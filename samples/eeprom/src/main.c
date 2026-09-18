/*
 * microconf EEPROM sample on Zephyr RTOS
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/logging/log.h>

#include "mconf.h"
#include "mconf_zephyr.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* 1. Define application configuration struct */
struct app_config {
    uint8_t dhcp_enabled;
    uint16_t port;
    uint32_t baudrate;
    char node_name[16];
};

static const uint16_t default_port = 8080u;
static const uint32_t default_baud = 115200u;

/* 2. Define schema entries with explicit defaults */
static const mconf_entry_t config_entries[] = {
    MCONF_ENTRY_SCALAR(struct app_config, dhcp_enabled, MCONF_TYPE_BOOL, &(const uint8_t){ 1u }),
    MCONF_ENTRY_SCALAR(struct app_config, port, MCONF_TYPE_U16, &default_port),
    MCONF_ENTRY_SCALAR(struct app_config, baudrate, MCONF_TYPE_U32, &default_baud),
    MCONF_ENTRY_STRING(struct app_config, node_name, "zephyr-node-01"),
};

/* 3. Define schema */
static const mconf_schema_t config_schema = {
    .entries = config_entries,
    .entry_count = sizeof(config_entries) / sizeof(config_entries[0]),
    .schema_version = 1u,
    .data_size = sizeof(struct app_config),
};

static struct app_config g_app_cfg;
static mconf_t g_mconf_ctx;
static mconf_io_t g_eeprom_io;
static struct mconf_zephyr_eeprom_ctx g_eeprom_ctx;

int main(void)
{
    const struct device *eeprom_dev;
    mconf_err_t err;
    int rc;

    LOG_INF("=== microconf Zephyr EEPROM Sample ===");

    /* Resolve EEPROM device from Devicetree */
#if DT_NODE_EXISTS(DT_ALIAS(eeprom_0))
    eeprom_dev = DEVICE_DT_GET(DT_ALIAS(eeprom_0));
#elif DT_HAS_COMPAT_STATUS_OKAY(zephyr_i2c_target_eeprom)
    eeprom_dev = DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_i2c_target_eeprom));
#elif DT_HAS_COMPAT_STATUS_OKAY(atmel_at24)
    eeprom_dev = DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(atmel_at24));
#elif DT_HAS_COMPAT_STATUS_OKAY(zephyr_emu_eeprom)
    eeprom_dev = DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_emu_eeprom));
#else
    eeprom_dev = DEVICE_DT_GET_ANY(eeprom);
#endif

    if (!device_is_ready(eeprom_dev)) {
        LOG_ERR("No ready EEPROM device found. Ensure devicetree has an enabled EEPROM node.");
        return 0;
    }

    /* Initialize microconf context with schema and RAM buffer */
    err = mconf_init(&g_mconf_ctx, sizeof(g_mconf_ctx), &config_schema,
                     &g_app_cfg, sizeof(g_app_cfg));
    if (err != MCONF_OK) {
        LOG_ERR("mconf_init failed: %s (%d)", mconf_err_str(err), (int)err);
        return 0;
    }

    /* Initialize Zephyr EEPROM persistence backend */
    rc = mconf_zephyr_eeprom_init(&g_eeprom_ctx, eeprom_dev, 0u, 0u, &g_eeprom_io);
    if (rc != 0) {
        LOG_ERR("mconf_zephyr_eeprom_init failed: %d", rc);
        return 0;
    }

    /* Attempt to load persisted configuration */
    err = mconf_load(&g_mconf_ctx, &g_eeprom_io);
    if (err == MCONF_OK) {
        LOG_INF("Successfully loaded config from EEPROM!");
    } else {
        LOG_WRN("No valid configuration found in EEPROM (%s). Loading defaults and saving...",
                mconf_err_str(err));
        mconf_load_defaults(&g_mconf_ctx);
        err = mconf_save(&g_mconf_ctx, &g_eeprom_io);
        if (err == MCONF_OK) {
            LOG_INF("Defaults saved to EEPROM slot.");
        } else {
            LOG_ERR("Failed to save defaults to EEPROM: %s", mconf_err_str(err));
        }
    }

    /* Print current configuration */
    LOG_INF("Current Configuration:");
    LOG_INF("  node_name   : %s", g_app_cfg.node_name);
    LOG_INF("  port        : %u", g_app_cfg.port);
    LOG_INF("  baudrate    : %u", g_app_cfg.baudrate);
    LOG_INF("  dhcp_enabled: %s", g_app_cfg.dhcp_enabled ? "true" : "false");

#if defined(CONFIG_MICROCONF_SHELL)
    /* Register with interactive shell: allows 'mconf dump app', 'mconf set app port 9090', etc. */
    rc = mconf_zephyr_shell_register(&g_mconf_ctx, &g_eeprom_io, "app");
    if (rc == 0) {
        LOG_INF("Registered with shell as 'app'. Try 'mconf dump app' or 'mconf --help' in console.");
    }
#endif

    return 0;
}
