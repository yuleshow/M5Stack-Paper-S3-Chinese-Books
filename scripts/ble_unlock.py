#!/usr/bin/env python3
"""
BLE Proximity Unlock — macOS Companion Script

Monitors the BLE signal from the M5Stack Paper S3 ("M5Paper-BLE").
When the device goes out of range  → locks the Mac.
When the device comes back in range → wakes the screen and types the password locally.

The Paper S3 acts purely as a BLE beacon — no BLE HID keyboard pairing needed.
The companion script handles all lock/unlock logic on the Mac.

Requirements:
    pip install bleak

Usage:
    python3 ble_unlock.py --password 'mypass'       # Required: Mac password
    python3 ble_unlock.py --name "MyDevice"         # Custom device name
    python3 ble_unlock.py --lock-threshold -80      # Adjust RSSI thresholds
    python3 ble_unlock.py --install-service          # Install as macOS LaunchAgent

Configuration is auto-saved to ~/.config/ble_unlock.json after first run.
Password is stored separately in macOS Keychain (not in the config file).
"""

import argparse
import asyncio
import json
import logging
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

try:
    from bleak import BleakScanner
except ImportError:
    print("Error: 'bleak' library required. Install with:")
    print("  pip install bleak")
    sys.exit(1)

# ── Configuration ─────────────────────────────────────────────────
CONFIG_PATH = Path.home() / ".config" / "ble_unlock.json"
KEYCHAIN_SERVICE = "ble-unlock-m5paper"
KEYCHAIN_ACCOUNT = "mac-password"

DEFAULTS = {
    "device_name": "M5Paper-BLE",
    "rssi_lock_threshold": -85,     # Lock when RSSI drops below this
    "rssi_unlock_threshold": -70,   # Unlock when RSSI rises above this
    "lock_delay_seconds": 15,       # Seconds of absence before locking
    "scan_interval_seconds": 3,     # How often to scan for BLE
    "log_level": "INFO",
}

logging.basicConfig(
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("ble_unlock")


# ── Keychain helpers ──────────────────────────────────────────────
def save_password_to_keychain(password: str):
    """Store password securely in macOS Keychain."""
    # Delete old entry if it exists (ignore errors)
    subprocess.run(
        ["security", "delete-generic-password",
         "-s", KEYCHAIN_SERVICE, "-a", KEYCHAIN_ACCOUNT],
        capture_output=True
    )
    result = subprocess.run(
        ["security", "add-generic-password",
         "-s", KEYCHAIN_SERVICE, "-a", KEYCHAIN_ACCOUNT,
         "-w", password],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        log.error("Failed to save password to Keychain: %s", result.stderr.strip())
    else:
        log.info("🔑 Password saved to macOS Keychain")


def load_password_from_keychain() -> str:
    """Retrieve password from macOS Keychain."""
    result = subprocess.run(
        ["security", "find-generic-password",
         "-s", KEYCHAIN_SERVICE, "-a", KEYCHAIN_ACCOUNT, "-w"],
        capture_output=True, text=True
    )
    if result.returncode == 0:
        return result.stdout.strip()
    return ""


def load_config(overrides: dict) -> dict:
    """Load config from file, apply overrides, save back."""
    cfg = dict(DEFAULTS)
    if CONFIG_PATH.exists():
        try:
            with open(CONFIG_PATH) as f:
                saved = json.load(f)
                # Migrate: remove password from old config files
                saved.pop("password", None)
                cfg.update(saved)
        except Exception as e:
            log.warning("Error reading config: %s", e)
    cfg.update({k: v for k, v in overrides.items()
                if v is not None and k != "password"})
    # Save config (without password)
    CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    save_cfg = {k: v for k, v in cfg.items() if k != "password"}
    with open(CONFIG_PATH, "w") as f:
        json.dump(save_cfg, f, indent=2)
    # Restrict config file permissions
    os.chmod(CONFIG_PATH, 0o600)
    return cfg


# ── macOS Screen Lock / Unlock ────────────────────────────────────
def lock_screen():
    """Lock the macOS screen."""
    log.info("🔒 Locking screen")
    subprocess.run(
        ["osascript", "-e",
         'tell application "System Events" to keystroke "q" using {control down, command down}'],
        capture_output=True, timeout=5
    )


def wake_screen():
    """Wake the display using caffeinate."""
    try:
        subprocess.run(["caffeinate", "-u", "-t", "2"], capture_output=True, timeout=5)
    except Exception:
        pass


def type_password(password: str):
    """Type the password on the lock screen using AppleScript."""
    log.info("🔓 Typing password on lock screen...")
    try:
        wake_screen()
        time.sleep(1.5)

        # Escape special characters for AppleScript string
        escaped = password.replace("\\", "\\\\").replace('"', '\\"')

        script = (
            'tell application "System Events"\n'
            f'  keystroke "{escaped}"\n'
            '  delay 0.3\n'
            '  keystroke return\n'
            'end tell'
        )
        subprocess.run(
            ["osascript", "-e", script],
            capture_output=True, text=True, timeout=10
        )
        log.info("✅ Password typed")
    except Exception as e:
        log.error("Failed to type password: %s", e)


# ── Main Loop ─────────────────────────────────────────────────────
async def main_loop(cfg: dict, password: str):
    device_name = cfg["device_name"]
    lock_threshold = cfg["rssi_lock_threshold"]
    unlock_threshold = cfg["rssi_unlock_threshold"]
    lock_delay = cfg["lock_delay_seconds"]
    scan_interval = cfg["scan_interval_seconds"]

    # last_strong_seen: last time RSSI was above lock_threshold
    # This ensures weak-signal scans don't reset the lock timer.
    last_strong_seen = time.time()
    is_near = False
    locked_by_us = False
    consecutive_absent = 0  # Count consecutive "not found" scans

    log.info("Monitoring for BLE device: %s", device_name)
    log.info("Lock threshold: %d dBm, Unlock threshold: %d dBm", lock_threshold, unlock_threshold)
    log.info("Lock delay: %d seconds, Scan interval: %d seconds", lock_delay, scan_interval)
    log.info("Config saved to: %s", CONFIG_PATH)
    log.info("Password: %s", "configured ✓" if password else "NOT SET ✗")
    log.info("─" * 50)

    while True:
        try:
            devices = await BleakScanner.discover(timeout=2.0, return_adv=True)
            found = None
            found_adv = None
            for addr, (d, adv) in devices.items():
                if d.name and device_name in d.name:
                    found = d
                    found_adv = adv
                    break

            if found:
                rssi = found_adv.rssi
                consecutive_absent = 0

                if rssi >= unlock_threshold:
                    # Strong signal — device is nearby
                    last_strong_seen = time.time()
                    if not is_near:
                        log.info("📶 Device nearby (RSSI: %d dBm)", rssi)
                        is_near = True
                        if locked_by_us:
                            if password:
                                type_password(password)
                            else:
                                log.warning("No password configured — can't auto-unlock")
                        locked_by_us = False
                    else:
                        log.debug("📶 Signal OK (RSSI: %d dBm)", rssi)
                elif rssi >= lock_threshold:
                    # Moderate signal — in the hysteresis zone
                    # Don't update last_strong_seen (so lock timer can still fire)
                    # Don't change is_near state (maintain current state)
                    log.debug("📶 Signal moderate (RSSI: %d dBm)", rssi)
                else:
                    # Weak signal — treat as "going away"
                    if is_near:
                        log.info("📶 Signal weak (RSSI: %d dBm), starting lock countdown...", rssi)
                        is_near = False
            else:
                consecutive_absent += 1
                if is_near:
                    elapsed = time.time() - last_strong_seen
                    log.info("📶 Device not found, absent for %.0fs...", elapsed)
                    is_near = False

            # Lock logic: based on time since last strong signal
            if not locked_by_us:
                elapsed_since_strong = time.time() - last_strong_seen
                if elapsed_since_strong > lock_delay and (not found or found_adv.rssi < lock_threshold):
                    lock_screen()
                    locked_by_us = True

        except Exception as e:
            log.error("Scan error: %s", e)

        await asyncio.sleep(scan_interval)


# ── LaunchAgent installer ─────────────────────────────────────────
def install_launchagent():
    """Install as a macOS LaunchAgent for auto-start on login."""
    # Verify password is in Keychain before installing
    pw = load_password_from_keychain()
    if not pw:
        print("⚠️  No password in Keychain. Run with --password first:")
        print("   python3 ble_unlock.py --password 'mypass'")
        print("   Then run --install-service again.")
        return

    plist_name = "com.m5paper.ble-unlock"
    plist_path = Path.home() / "Library" / "LaunchAgents" / f"{plist_name}.plist"
    script_path = os.path.abspath(__file__)
    python_path = sys.executable

    plist_content = f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>{plist_name}</string>
    <key>ProgramArguments</key>
    <array>
        <string>{python_path}</string>
        <string>{script_path}</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardOutPath</key>
    <string>/tmp/ble_unlock.log</string>
    <key>StandardErrorPath</key>
    <string>/tmp/ble_unlock.err</string>
</dict>
</plist>"""

    plist_path.parent.mkdir(parents=True, exist_ok=True)
    plist_path.write_text(plist_content)
    print(f"✅ LaunchAgent installed: {plist_path}")
    print(f"   Password: loaded from Keychain ✓")
    print(f"   Start now: launchctl load {plist_path}")
    print(f"   Stop:      launchctl unload {plist_path}")
    print(f"   Logs:      tail -f /tmp/ble_unlock.log")


# ── Entry point ───────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="BLE Proximity Unlock — macOS companion for M5Stack Paper S3"
    )
    parser.add_argument("--name", dest="device_name",
                        help=f"BLE device name to monitor (default: {DEFAULTS['device_name']})")
    parser.add_argument("--password", dest="password",
                        help="Your Mac login password (stored in macOS Keychain)")
    parser.add_argument("--lock-threshold", type=int, dest="rssi_lock_threshold",
                        help=f"RSSI to trigger lock (default: {DEFAULTS['rssi_lock_threshold']})")
    parser.add_argument("--unlock-threshold", type=int, dest="rssi_unlock_threshold",
                        help=f"RSSI to trigger unlock (default: {DEFAULTS['rssi_unlock_threshold']})")
    parser.add_argument("--lock-delay", type=int, dest="lock_delay_seconds",
                        help=f"Seconds before locking (default: {DEFAULTS['lock_delay_seconds']})")
    parser.add_argument("--scan-interval", type=int, dest="scan_interval_seconds",
                        help=f"Scan interval seconds (default: {DEFAULTS['scan_interval_seconds']})")
    parser.add_argument("--debug", action="store_true",
                        help="Enable debug logging")
    parser.add_argument("--install-service", action="store_true",
                        help="Install as macOS LaunchAgent")
    args = parser.parse_args()

    if args.install_service:
        install_launchagent()
        return

    overrides = {k: v for k, v in vars(args).items()
                 if k not in ("debug", "install_service", "password")}
    cfg = load_config(overrides)

    # Handle password: CLI → Keychain
    if args.password:
        save_password_to_keychain(args.password)
    password = load_password_from_keychain()

    log_level = "DEBUG" if args.debug else cfg.get("log_level", "INFO")
    log.setLevel(log_level)

    print("╔══════════════════════════════════════════╗")
    print("║   BLE Proximity Unlock — M5Paper S3     ║")
    print("╚══════════════════════════════════════════╝")
    print()

    if not password:
        print("⚠️  No password configured. Auto-unlock will be disabled.")
        print("   Set with: python3 ble_unlock.py --password 'mypass'")
        print()

    # Handle graceful shutdown
    def shutdown(sig, frame):
        print("\nShutting down...")
        sys.exit(0)
    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    asyncio.run(main_loop(cfg, password))


if __name__ == "__main__":
    main()
