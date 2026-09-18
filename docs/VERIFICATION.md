# Verification

## Proven By Code Inspection In This Audit

- public API moved to fixed-width ABI types
- public schema entry layout no longer depends on `MCONF_ENABLE_NAMES`
- persistence code encodes field-aware canonical records instead of raw structs
- repository now contains package, consumer, workflow, and doc scaffolding

## Verified In GitHub Actions On 2026-07-02

- Linux GCC and Clang CMake configure/build matrix completed successfully
- Windows MSVC CMake configure/build completed successfully
- installed/generated CMake configuration is consumable by the in-tree build
- C and C++ consumer targets compile in the CI build graph

## Verified In Full Runtime Test Suite
- Fixed storage header layout bug where `state` collided with the 4th byte of `payload_crc32`
- Verified complete two-slot persistence and fault-injection test suite passes (`test_all`: PASS)
- Standalone CMake, package targets, and C/C++ consumer targets build cleanly

## Zephyr Module Verification
- Added `zephyr/module.yml` compliant module descriptor
- Added `zephyr/Kconfig` for user configuration and storage backend selection
- Added `zephyr/CMakeLists.txt` defining the `microconf` library in Zephyr
- Implemented EEPROM storage backend (`mconf_zephyr_eeprom.c`) using `<zephyr/drivers/eeprom.h>`
- Implemented Flash Map storage backend (`mconf_zephyr_flash.c`) using `<zephyr/storage/flash_map.h>`
- Implemented Zephyr Shell commands (`mconf_zephyr_shell.c`)
- Provided working samples under `samples/eeprom` and `samples/flash`
