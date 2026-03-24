"""
PlatformIO pre-build script: patch OpenFontRender to use PSRAM for FreeType
allocations on ESP32-S3.

The OFR library's FileSupport.h checks CONFIG_SPIRAM_SUPPORT (ESP-IDF v3 macro)
but ESP-IDF v5 defines CONFIG_SPIRAM instead. This script patches the check to
accept both macros and adds forward declarations for ps_malloc/ps_calloc/
ps_realloc, so FreeType uses PSRAM instead of the ~280KB internal heap.
"""
Import("env")
import os


def patch_ofr_filesupport():
    libdeps = env.get("PROJECT_LIBDEPS_DIR", ".pio/libdeps")
    pioenv = env.get("PIOENV", "m5stack_paper")
    filepath = os.path.join(libdeps, pioenv,
                            "OpenFontRender", "src", "FileSupport.h")
    if not os.path.exists(filepath):
        print("  [patch_ofr] FileSupport.h not found, skipping")
        return

    with open(filepath, "r") as f:
        content = f.read()

    changed = False

    # Add sdkconfig.h include so CONFIG_SPIRAM is visible
    if '#include "sdkconfig.h"' not in content and "#include <sdkconfig.h>" not in content:
        content = content.replace(
            "#include <cstddef>",
            '#include <cstddef>\n#include "sdkconfig.h"'
        )
        changed = True

    # Check both old (v3) and new (v5) SPIRAM config macros, and add
    # forward declarations for ps_malloc/ps_calloc/ps_realloc
    old_block = """#ifdef CONFIG_SPIRAM_SUPPORT
\t#define ft_scalloc ps_calloc
\t#define ft_smalloc ps_malloc
\t#define ft_srealloc ps_realloc"""
    new_block = """#if defined(CONFIG_SPIRAM_SUPPORT) || defined(CONFIG_SPIRAM)
\t#ifdef __cplusplus
\textern "C" {
\t#endif
\tvoid *ps_malloc(size_t size);
\tvoid *ps_calloc(size_t n, size_t size);
\tvoid *ps_realloc(void *ptr, size_t size);
\t#ifdef __cplusplus
\t}
\t#endif
\t#define ft_scalloc ps_calloc
\t#define ft_smalloc ps_malloc
\t#define ft_srealloc ps_realloc"""

    if old_block in content:
        content = content.replace(old_block, new_block)
        changed = True

    if changed:
        with open(filepath, "w") as f:
            f.write(content)
        print("  [patch_ofr] Patched FileSupport.h for PSRAM support")
    else:
        print("  [patch_ofr] FileSupport.h already patched or not needed")


# Run patch immediately at script load time (before compilation starts)
patch_ofr_filesupport()
