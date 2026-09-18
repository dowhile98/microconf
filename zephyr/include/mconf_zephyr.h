/*
 * microconf Zephyr Integration Header
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MCONF_ZEPHYR_H
#define MCONF_ZEPHYR_H

#include "mconf.h"

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * EEPROM Storage Backend
 * -------------------------------------------------------------------------- */
#if defined(CONFIG_MICROCONF_BACKEND_EEPROM)

#include <zephyr/drivers/eeprom.h>

/**
 * @brief Context structure for the Zephyr EEPROM backend.
 *
 * The caller allocates this structure and passes it to mconf_zephyr_eeprom_init().
 * It must remain valid for the lifetime of the associated mconf_io_t instance.
 */
struct mconf_zephyr_eeprom_ctx {
    const struct device *dev;
    size_t base_offset;
    size_t total_size;
};

/**
 * @brief Initialize an mconf_io_t instance backed by a Zephyr EEPROM device.
 *
 * Configures a two-slot ping-pong storage region inside the specified EEPROM device.
 *
 * @param ctx Pointer to caller-allocated context structure.
 * @param dev Pointer to the EEPROM device (e.g. DEVICE_DT_GET(...)).
 * @param base_offset Starting offset in bytes inside the EEPROM for microconf.
 * @param total_size Total bytes allocated for microconf across both slots (must be >= 64, even).
 *                   If set to 0, the remaining size of the EEPROM is used.
 * @param io Pointer to the mconf_io_t structure to populate.
 *
 * @return 0 on success, negative error code on failure (-EINVAL, -ENODEV).
 */
int mconf_zephyr_eeprom_init(struct mconf_zephyr_eeprom_ctx *ctx,
                             const struct device *dev,
                             size_t base_offset,
                             size_t total_size,
                             mconf_io_t *io);

#endif /* CONFIG_MICROCONF_BACKEND_EEPROM */

/* --------------------------------------------------------------------------
 * Flash Map (Partition) Storage Backend
 * -------------------------------------------------------------------------- */
#if defined(CONFIG_MICROCONF_BACKEND_FLASH)

#include <zephyr/storage/flash_map.h>

/**
 * @brief Context structure for the Zephyr Flash Map backend.
 *
 * The caller allocates this structure and passes it to mconf_zephyr_flash_init().
 * It must remain valid for the lifetime of the associated mconf_io_t instance.
 */
struct mconf_zephyr_flash_ctx {
    const struct flash_area *fa;
    uint8_t area_id;
    size_t base_offset;
    size_t total_size;
};

/**
 * @brief Initialize an mconf_io_t instance backed by a Zephyr Flash Map partition.
 *
 * Opens the specified partition and configures a two-slot ping-pong storage region.
 *
 * @param ctx Pointer to caller-allocated context structure.
 * @param area_id Zephyr flash area ID (e.g. FIXED_PARTITION_ID(storage_partition)).
 * @param io Pointer to the mconf_io_t structure to populate.
 *
 * @return 0 on success, negative error code on failure (-EINVAL, -ENOENT, -EIO).
 */
int mconf_zephyr_flash_init(struct mconf_zephyr_flash_ctx *ctx,
                            uint8_t area_id,
                            mconf_io_t *io);

/**
 * @brief Release and close the flash area associated with the context.
 *
 * @param ctx Pointer to the context structure.
 */
void mconf_zephyr_flash_close(struct mconf_zephyr_flash_ctx *ctx);

#endif /* CONFIG_MICROCONF_BACKEND_FLASH */

/* --------------------------------------------------------------------------
 * Zephyr Shell Integration
 * -------------------------------------------------------------------------- */
#if defined(CONFIG_MICROCONF_SHELL)

/**
 * @brief Register an active microconf context with the Zephyr shell command handler.
 *
 * Allows inspecting ('mconf dump'), reading ('mconf get'), updating ('mconf set'),
 * and persisting ('mconf save', 'mconf load', 'mconf defaults') configuration
 * schemas from the interactive Zephyr shell console.
 *
 * @param ctx Pointer to initialized mconf_t context.
 * @param io Pointer to optional mconf_io_t persistence backend (can be NULL).
 * @param name Friendly name for this configuration in the shell (e.g. "sys", "net").
 *
 * @return 0 on success, -ENOMEM if registry is full, -EINVAL if invalid arguments.
 */
int mconf_zephyr_shell_register(mconf_t *ctx, const mconf_io_t *io, const char *name);

#endif /* CONFIG_MICROCONF_SHELL */

#ifdef __cplusplus
}
#endif

#endif /* MCONF_ZEPHYR_H */
