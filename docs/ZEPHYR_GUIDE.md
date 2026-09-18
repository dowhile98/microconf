# Zephyr RTOS Integration Guide

`microconf` is packaged as an official Zephyr module (`zephyr/module.yml`), allowing you to integrate fixed-memory configuration management directly into your Zephyr projects.

## 1. Adding microconf to your Zephyr Workspace

### Option A: Via West Manifest (`west.yml`)
Add `microconf` under `projects` in your application or workspace manifest:

```yaml
manifest:
  projects:
    - name: microconf
      url: https://github.com/dowhile98/microconf.git
      revision: master
      path: modules/lib/microconf
```

Then synchronize your workspace:
```bash
west update
```

### Option B: Out-of-tree / Direct Module Path
When invoking `west build`, specify the module path directly:
```bash
west build -b <your_board> -DZEPHYR_MODULES=/path/to/microconf
```

---

## 2. Kconfig Options

Add desired options to your application's `prj.conf`:

```ini
# Core microconf enablement
CONFIG_MICROCONF=y
CONFIG_MICROCONF_MAX_ENTRIES=64
CONFIG_MICROCONF_MAX_KEY_LEN=48
CONFIG_MICROCONF_ENABLE_NAMES=y
CONFIG_MICROCONF_ENABLE_FLOAT=y

# Storage backends
CONFIG_EEPROM=y
CONFIG_MICROCONF_BACKEND_EEPROM=y

# Or Flash Map partition backend:
# CONFIG_FLASH=y
# CONFIG_FLASH_MAP=y
# CONFIG_MICROCONF_BACKEND_FLASH=y

# Interactive Zephyr Shell integration
CONFIG_SHELL=y
CONFIG_MICROCONF_SHELL=y
```

---

## 3. Storage Backends

### A. EEPROM Backend (`<zephyr/drivers/eeprom.h>`)
EEPROMs are byte-addressable and permit in-place byte overwriting without requiring block erases. This is the optimal physical storage medium for `microconf`.

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/eeprom.h>
#include <mconf.h>
#include <mconf_zephyr.h>

static mconf_t g_mconf_ctx;
static mconf_io_t g_eeprom_io;
static struct mconf_zephyr_eeprom_ctx g_eeprom_ctx;
static struct my_config g_cfg;

void init_config(void)
{
    const struct device *eeprom = DEVICE_DT_GET(DT_ALIAS(eeprom_0));

    mconf_init(&g_mconf_ctx, sizeof(g_mconf_ctx), &my_schema, &g_cfg, sizeof(g_cfg));

    /* Initialize EEPROM backend: offset 0, remaining capacity, populates g_eeprom_io */
    mconf_zephyr_eeprom_init(&g_eeprom_ctx, eeprom, 0, 0, &g_eeprom_io);

    if (mconf_load(&g_mconf_ctx, &g_eeprom_io) != MCONF_OK) {
        mconf_load_defaults(&g_mconf_ctx);
        mconf_save(&g_mconf_ctx, &g_eeprom_io);
    }
}
```

### B. Flash Map Backend (`<zephyr/storage/flash_map.h>`)
Uses pre-allocated flash partitions defined in your board's Devicetree (e.g. `storage_partition`):

```c
#include <zephyr/storage/flash_map.h>
#include <mconf.h>
#include <mconf_zephyr.h>

static mconf_t g_mconf_ctx;
static mconf_io_t g_flash_io;
static struct mconf_zephyr_flash_ctx g_flash_ctx;

void init_flash_config(void)
{
    mconf_init(&g_mconf_ctx, sizeof(g_mconf_ctx), &my_schema, &g_cfg, sizeof(g_cfg));

    /* Initialize partition backend using FIXED_PARTITION_ID */
    mconf_zephyr_flash_init(&g_flash_ctx, FIXED_PARTITION_ID(storage_partition), &g_flash_io);

    if (mconf_load(&g_mconf_ctx, &g_flash_io) != MCONF_OK) {
        mconf_load_defaults(&g_mconf_ctx);
        mconf_save(&g_mconf_ctx, &g_flash_io);
    }
}
```

---

## 4. Zephyr Shell Commands

When `CONFIG_MICROCONF_SHELL=y` is enabled, register your context during startup:

```c
mconf_zephyr_shell_register(&g_mconf_ctx, &g_eeprom_io, "app");
```

In the Zephyr serial console:
```text
uart:~$ mconf list
Registered microconf configurations:
  [app] - 4 schema entries, 24 bytes data, persistence: yes

uart:~$ mconf dump app
=== Schema dump: app (v1, 4 entries) ===
  [ 0] dhcp_enabled             (bool   ,  1B): true
  [ 1] port                     (u16    ,  2B): 8080
  [ 2] baudrate                 (u32    ,  4B): 115200
  [ 3] node_name                (string , 16B): "zephyr-node-01"

uart:~$ mconf get app port
8080

uart:~$ mconf set app port 9090
Key 'port' updated successfully.

uart:~$ mconf save app
Configuration 'app' saved to persistent storage.
```

---

## 5. Samples

The repository includes ready-to-build samples under `samples/`:
- `samples/eeprom`: Demonstrates EEPROM backend and shell integration.
- `samples/flash`: Demonstrates Flash Map partition backend and shell integration.
