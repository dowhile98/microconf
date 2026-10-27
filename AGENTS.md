# AGENTS.md

## Repository Overview

`microconf` is a zero-heap, caller-owned C99 configuration storage library for embedded systems (bare-metal, Zephyr RTOS, POSIX). It enforces fixed memory, validated schemas with compile-time fingerprints, and a canonical two-slot ping-pong storage layout (32-byte header, generation sequence, CRC32 payload checksum).

- **Core standard**: C99 (`target_compile_features(microconf PUBLIC c_std_99)`)
- **Key invariants**: Zero dynamic memory allocation (`malloc`/`free` forbidden), no background threads, no locks, no mutable global state. Concurrency and serialization must be handled by the caller. CRC32 is strictly corruption detection, not cryptographic authentication. Raw C structs are never written directly to storage.

---

## Build & Test Commands

### Fast Standalone Smoke Test (Host)
Bypasses CMake using the repo-local test Makefile:
```bash
make -C tests
```
Builds `tests/test_all.c` against `src/mconf.c` and runs `./test_all`.

### CMake Build & Full Test Suite
```bash
# Configure with strict warnings and tests enabled
cmake -S . -B build -DMCONF_BUILD_TESTS=ON -DMCONF_STRICT_WARNINGS=ON

# Build library, smoke test, and consumer targets
cmake --build build --parallel

# Run primary smoke test
./build/microconf_test

# Run consumer target verification
./build/tests/consumers/add_subdirectory/microconf_add_subdirectory_consumer
./build/tests/consumers/cpp/microconf_cpp11
./build/tests/consumers/cpp/microconf_cpp17
./build/tests/consumers/cpp/microconf_cpp20
```

### Post-Install Package Consumer Verification
Tests `find_package(microconf CONFIG REQUIRED)`:
```bash
cmake --install build --prefix /tmp/mconf_install
cmake -S tests/consumers/find_package -B build/find_pkg_test -DCMAKE_PREFIX_PATH=/tmp/mconf_install
cmake --build build/find_pkg_test
./build/find_pkg_test/microconf_find_package_consumer
```

### Zephyr Sample Builds
When inside a Zephyr workspace or building with Zephyr SDK:
```bash
west build -b native_sim samples/eeprom
west build -b native_sim samples/flash
```

---

## Architecture & Directory Layout

- `include/mconf.h`: Public C ABI types (`mconf_err_t`, `mconf_type_t`, `mconf_t`, `mconf_schema_t`, `mconf_io_t`) and API macros.
- `include/mconf_config.h`: Authoritative configuration fallback header for non-CMake builds and Zephyr autoconf bridging.
- `src/mconf.c`: Single-file core runtime: CRC32 computation, schema validation, fingerprinting, typed get/set accessors, and two-slot canonical serializer.
- `cmake/`: Template files (`mconf_config.h.in`, `microconfConfig.cmake.in`) for CMake package generation.
- `zephyr/`: Zephyr RTOS module packaging (`module.yml`, `Kconfig`, `CMakeLists.txt`), driver backends (`mconf_zephyr_eeprom.c`, `mconf_zephyr_flash.c`), and shell commands (`mconf_zephyr_shell.c`).
- `samples/`: Self-contained Zephyr application samples (`samples/eeprom`, `samples/flash`).
- `tests/`: Smoke test harness (`test_all.c`), standalone `Makefile`, consumer integration tests (`tests/consumers/`), and negative compilation tests (`tests/compile_fail/`).

---

## Critical Toolchain & Configuration Quirks

1. **Dual Build-System Mode**:
   - In root `CMakeLists.txt`, `if(DEFINED ZEPHYR_BASE) return() endif()` ensures that when built as a Zephyr module, the root CMake logic yields to `zephyr/CMakeLists.txt`.
   - Never remove or reorder the `ZEPHYR_BASE` guard in root `CMakeLists.txt`.

2. **Authoritative Config Immutability**:
   - `mconf_config.h` defines authoritative values for `MCONF_CONFIG_MAX_ENTRIES` (default 64), `MCONF_CONFIG_MAX_KEY_LEN` (default 48), `MCONF_CONFIG_ENABLE_NAMES` (default 1), and `MCONF_CONFIG_ENABLE_FLOAT` (default 1).
   - In `include/mconf.h`, conflicting preprocessor overrides (e.g. `-DMCONF_MAX_ENTRIES=128` without updating config) trigger `#error` at compile time.
   - For CMake builds, configure options via `-DMCONF_CONFIG_MAX_ENTRIES=...` or `-DMCONF_CONFIG_ENABLE_NAMES=OFF`.

3. **Strict Compiler Warnings**:
   - With `MCONF_STRICT_WARNINGS=ON` (default), GCC/Clang compiles with `-Wall -Wextra -Wpedantic -Wconversion -Werror` and MSVC with `/W4 /WX`. All integer conversions and size casts must be explicit.

4. **Negative Compilation Tests (`tests/compile_fail/`)**:
   - Files in `tests/compile_fail/` (`conflicting_config.c`, `invalid_schema_count.c`, `invalid_string_default.c`) MUST NOT be added to CMake build targets. They are designed to fail compilation during CI static validation.

---

## Development & Contract Rules

- **Schema Defaults**:
  - `MCONF_ENTRY_SCALAR`: `default_ptr` must point to data matching field size.
  - `MCONF_ENTRY_STRING`: Length is stored excluding the trailing NUL terminator (`sizeof(literal) - 1`). Buffer in data struct must allocate space for NUL terminator.
  - `MCONF_ENTRY_BLOB`: Default byte length must match field size exactly.
- **Persistence Contract**:
  - Requires caller-supplied `mconf_io_t` with `read`, `write`, `erase` function pointers and `storage_size >= 2 * slot_size`.
  - Storage is split into two equal slots (`slot 0` at offset 0, `slot 1` at offset `slot_size`).
  - Backend callbacks must NEVER recursively invoke `mconf_*` functions on the same `mconf_t` context.
- **C++ Consumers**:
  - Entry macros avoid designated initializers for C++ compatibility. Structs used in schema definitions must have standard layout (`offsetof` requirement).
