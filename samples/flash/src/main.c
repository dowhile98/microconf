/*
 * microconf Flash Map partition sample on Zephyr RTOS
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>

#include "mconf.h"
#include "mconf_zephyr.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* 1. Define application configuration struct */
struct device_config {
    uint8_t alert_enabled;
    uint16_t sample_rate_hz;
    uint32_t device_id;
    char ssid[20];
};

static const uint16_t default_sample_rate = 100u;
static const uint32_t default_dev_id = 0x12345678u;

/* 2. Define schema entries with explicit defaults */
static const mconf_entry_t config_entries[] = {
    MCONF_ENTRY_SCALAR(struct device_config, alert_enabled, MCONF_TYPE_BOOL, &(const uint8_t){ 0u }),
    MCONF_ENTRY_SCALAR(struct device_config, sample_rate_hz, MCONF_TYPE_U16, &default_sample_rate),
    MCONF_ENTRY_SCALAR(struct device_config, device_id, MCONF_TYPE_U32, &default_dev_id),
    MCONF_ENTRY_STRING(struct device_config, ssid, "Zephyr-IoT-AP"),
};

/* 3. Define schema */
static const mconf_schema_t config_schema = {
    .entries = config_entries,
    .entry_count = sizeof(config_entries) / sizeof(config_entries[0]),
    .schema_version = 1u,
    .data_size = sizeof(struct device_config),
};

static struct device_config g_dev_cfg;
static mconf_t g_mconf_ctx;
static mconf_io_t g_flash_io;
static struct mconf_zephyr_flash_ctx g_flash_ctx;

int main(void)
{
    mconf_err_t err;
    int rc;

    LOG_INF("=== microconf Zephyr Flash Partition Sample ===");

    /* Initialize microconf context with schema and RAM buffer */
    err = mconf_init(&g_mconf_ctx, sizeof(g_mconf_ctx), &config_schema,
                     &g_dev_cfg, sizeof(g_dev_cfg));
    if (err != MCONF_OK) {
        LOG_ERR("mconf_init failed: %s (%d)", mconf_err_str(err), (int)err);
        return 0;
    }

    /* Open partition: standard storage_partition in devicetree */
#if FIXED_PARTITION_EXISTS(storage_partition)
    rc = mconf_zephyr_flash_init(&g_flash_ctx, FIXED_PARTITION_ID(storage_partition), &g_flash_io);
#else
    LOG_ERR("storage_partition does not exist in devicetree");
    return 0;
#endif

    if (rc != 0) {
        LOG_ERR("mconf_zephyr_flash_init failed: %d", rc);
        return 0;
    }

    /* Attempt to load persisted configuration */
    err = mconf_load(&g_mconf_ctx, &g_flash_io);
    if (err == MCONF_OK) {
        LOG_INF("Successfully loaded config from Flash partition!");
    } else {
        LOG_WRN("No valid configuration found in Flash (%s). Loading defaults and saving...",
                mconf_err_str(err));
        mconf_load_defaults(&g_mconf_ctx);
        err = mconf_save(&g_mconf_ctx, &g_flash_io);
        if (err == MCONF_OK) {
            LOG_INF("Defaults saved to Flash slot.");
        } else {
            LOG_ERR("Failed to save defaults to Flash: %s", mconf_err_str(err));
        }
    }

    /* Print current configuration */
    LOG_INF("Current Configuration:");
    LOG_INF("  ssid          : %s", g_dev_cfg.ssid);
    LOG_INF("  sample_rate_hz: %u Hz", g_dev_cfg.sample_rate_hz);
    LOG_INF("  device_id     : 0x%08x", g_dev_cfg.device_id);
    LOG_INF("  alert_enabled : %s", g_dev_cfg.alert_enabled ? "true" : "false");

#if defined(CONFIG_MICROCONF_SHELL)
    /* Register with interactive shell: allows 'mconf dump dev', 'mconf set dev ssid MyWiFi', etc. */
    rc = mconf_zephyr_shell_register(&g_mconf_ctx, &g_flash_io, "dev");
    if (rc == 0) {
        LOG_INF("Registered with shell as 'dev'. Try 'mconf dump dev' or 'mconf --help' in console.");
    }
#endif

    return 0;
}
