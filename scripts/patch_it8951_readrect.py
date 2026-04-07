"""
PlatformIO pre-build script: patch M5GFX Panel_IT8951 to fix three issues:

1. readRect() — use PSRAM for temporary buffers + null-pointer checks.
   The stock code calls heap_alloc() (internal SRAM) without null checks.
   When FreeType or WiFi reduce internal heap, the allocation fails and
   crashes. These buffers are not used for SPI DMA, so PSRAM is safe.

2. _write_args() — add timeout to bare busy-wait spin loop.
   The stock code has `while (!lgfx::gpio_in(_cfg.pin_busy));` with NO
   timeout. If the IT8951 is busy (e.g., during quality e-ink refresh),
   this spins forever and halts the system.

3. readRect() row loop — add WDT reset + error check per row.
   The inner loop calls _read_raw_line() for each row with no WDT feed.
   Each call can spin for seconds in busy-waits. With many rows per chunk
   this exceeds the 30-second WDT timeout and reboots the device.
"""
Import("env")
import os


def patch_it8951():
    libdeps = env.get("PROJECT_LIBDEPS_DIR", ".pio/libdeps")
    pioenv = env.get("PIOENV", "m5stack_paper")
    filepath = os.path.join(libdeps, pioenv,
                            "M5GFX", "src", "lgfx", "v1", "panel",
                            "Panel_IT8951.cpp")
    if not os.path.exists(filepath):
        print("  [patch_it8951] Panel_IT8951.cpp not found, skipping")
        return

    with open(filepath, "r") as f:
        content = f.read()

    changed = False

    # --- Patch 1: readRect PSRAM buffers + null checks ---
    old_alloc = (
        "auto readbuf = static_cast<uint8_t*>"
        "(heap_alloc(std::max(padding_len, rw * param->dst_bits >> 3)));\n"
        "    auto colorbuf = static_cast<bgr888_t*>"
        "(heap_alloc(rw * sizeof(bgr888_t)));\n"
    )

    new_alloc = (
        "auto readbuf = static_cast<uint8_t*>"
        "(heap_alloc_psram(std::max(padding_len, rw * param->dst_bits >> 3)));\n"
        "    auto colorbuf = static_cast<bgr888_t*>"
        "(heap_alloc_psram(rw * sizeof(bgr888_t)));\n"
        "    if (!readbuf || !colorbuf) {\n"
        "      heap_free(colorbuf);\n"
        "      heap_free(readbuf);\n"
        "      endWrite();\n"
        "      return;\n"
        "    }\n"
    )

    if old_alloc in content:
        content = content.replace(old_alloc, new_alloc)
        changed = True
        print("  [patch_it8951] Patched readRect: PSRAM buffers + null checks")

    # --- Patch 2: _write_args bare busy-wait → timeout ---
    # The stock code: while (!lgfx::gpio_in(_cfg.pin_busy));
    # Replace with a timeout loop (~2 seconds max)
    old_busy = "        while (!lgfx::gpio_in(_cfg.pin_busy));"
    new_busy = (
        "        { auto _t = millis();\n"
        "          while (!lgfx::gpio_in(_cfg.pin_busy)) {\n"
        "            if (millis() - _t > 2000) break;\n"
        "          }\n"
        "        }"
    )

    if old_busy in content and "auto _t = millis();" not in content:
        content = content.replace(old_busy, new_busy)
        changed = True
        print("  [patch_it8951] Patched _write_args: added busy-wait timeout")

    # --- Patch 3: readRect row loop — WDT reset + error check ---
    # Add #include <esp_task_wdt.h> so esp_task_wdt_reset() is available.
    old_include = (
        '#if __has_include (<esp_log.h>)\n'
        ' #include <esp_log.h>\n'
        '#endif'
    )
    new_include = (
        '#if __has_include (<esp_log.h>)\n'
        ' #include <esp_log.h>\n'
        '#endif\n'
        '\n'
        '#if __has_include (<esp_task_wdt.h>)\n'
        ' #include <esp_task_wdt.h>\n'
        '#endif'
    )

    if old_include in content and "esp_task_wdt.h" not in content:
        content = content.replace(old_include, new_include)
        changed = True
        print("  [patch_it8951] Added #include <esp_task_wdt.h>")

    # Check _read_raw_line return value and feed WDT after each row.
    old_readraw = (
        '      _read_raw_line(rx & ~3, ry, padding_len >> 1, '
        'reinterpret_cast<uint16_t*>(readbuf));'
    )
    new_readraw = (
        '      if (!_read_raw_line(rx & ~3, ry, padding_len >> 1, '
        'reinterpret_cast<uint16_t*>(readbuf))) break;\n'
        '#if __has_include(<esp_task_wdt.h>)\n'
        '      esp_task_wdt_reset();\n'
        '#endif'
    )

    if old_readraw in content and "if (!_read_raw_line" not in content:
        content = content.replace(old_readraw, new_readraw)
        changed = True
        print("  [patch_it8951] Patched readRect: WDT reset + error check")

    if changed:
        with open(filepath, "w") as f:
            f.write(content)
    elif ("heap_alloc_psram" in content
          and "auto _t = millis();" in content
          and "if (!_read_raw_line" in content):
        print("  [patch_it8951] Panel_IT8951.cpp already patched")
    else:
        print("  [patch_it8951] WARNING: patterns not found, "
              "library may have changed")


patch_it8951()
