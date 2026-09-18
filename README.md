# microconf

[![CI](https://github.com/Vanderhell/microconf/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/Vanderhell/microconf/actions/workflows/ci.yml?query=branch%3Amaster)
[![Release](https://img.shields.io/github/v/release/Vanderhell/microconf)](https://github.com/Vanderhell/microconf/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![C99](https://img.shields.io/badge/language-C99-blue.svg)](#build)
[![Docs](https://img.shields.io/badge/docs-verified-black.svg)](docs/VERIFICATION.md)

`microconf` is a small C99 configuration library for caller-owned embedded data.
It uses validated schemas, explicit default sizes, and a canonical two-slot
storage format. It does not allocate memory, does not spawn threads, and does
not claim automatic schema migration.

## Status

This repository is under repair and verification. The design targets fixed
memory, zero heap use, caller-owned lifetimes, and deterministic persistence,
but the included documentation calls out what is still `NOT VERIFIED`.

## Contracts

- Public ABI uses fixed-width `mconf_err_t` and `mconf_type_t`.
- `mconf_t` is a validated caller-owned context around schema, data, and fingerprint.
- Defaults are explicit: scalar defaults match field size, string defaults store
  byte length without the trailing NUL, blob defaults must match field size.
- Persistence is canonical, field-aware, and two-slot. Raw C structs are not
  written to storage.
- CRC32 detects corruption but is not authentication.

## Build

`microconf` provides:

- `CMakeLists.txt` with target `microconf`
- alias target `microconf::microconf`
- installable `microconfConfig.cmake` package metadata
- Zephyr RTOS module packaging (`zephyr/module.yml`, Kconfig, and backends)
- repo-local `tests/Makefile` that respects caller toolchain flags

## Docs

- [Zephyr RTOS guide](docs/ZEPHYR_GUIDE.md)
- [Cookbook](docs/COOKBOOK.md)
- [API reference](docs/API_REFERENCE.md)
- [Design](docs/DESIGN.md)
- [Porting guide](docs/PORTING_GUIDE.md)
- [Issues and troubleshooting](docs/ISSUES.md)
- [Verification status](docs/VERIFICATION.md)
- [Changelog](CHANGELOG.md)
- [Contributing](CONTRIBUTING.md)
- [Security](SECURITY.md)

Releases are tag-based only. See [CHANGELOG.md](CHANGELOG.md) and
[.github/workflows/release.yml](.github/workflows/release.yml).
