/*
 * microconf Zephyr EEPROM Storage Backend
 *
 * SPDX-License-Identifier: MIT
 */

#include "mconf_zephyr.h"

#include <inttypes.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(mconf_eeprom, CONFIG_MICROCONF_LOG_LEVEL);

#define MCONF_EEPROM_ERASE_CHUNK_SIZE 32u

static int mconf_zephyr_eeprom_read(void *callback_ctx, size_t offset, void *buffer, size_t size)
{
    struct mconf_zephyr_eeprom_ctx *ctx = (struct mconf_zephyr_eeprom_ctx *)callback_ctx;
    off_t target;
    int rc;

    if ((ctx == NULL) || (buffer == NULL)) {
        return -1;
    }
    if ((offset + size) > ctx->total_size) {
        return -1;
    }

    target = (off_t)(ctx->base_offset + offset);
    rc = eeprom_read(ctx->dev, target, buffer, size);
    if (rc < 0) {
        LOG_ERR("eeprom_read failed at offset 0x%" PRIxPTR " (size %zu): %d", (uintptr_t)target, size, rc);
        return -1;
    }
    return 0;
}

static int mconf_zephyr_eeprom_write(void *callback_ctx, size_t offset, const void *buffer, size_t size)
{
    struct mconf_zephyr_eeprom_ctx *ctx = (struct mconf_zephyr_eeprom_ctx *)callback_ctx;
    off_t target;
    int rc;

    if ((ctx == NULL) || (buffer == NULL)) {
        return -1;
    }
    if ((offset + size) > ctx->total_size) {
        return -1;
    }

    target = (off_t)(ctx->base_offset + offset);
    rc = eeprom_write(ctx->dev, target, buffer, size);
    if (rc < 0) {
        LOG_ERR("eeprom_write failed at offset 0x%" PRIxPTR " (size %zu): %d", (uintptr_t)target, size, rc);
        return -1;
    }
    return 0;
}

static int mconf_zephyr_eeprom_erase(void *callback_ctx, size_t offset, size_t size)
{
    struct mconf_zephyr_eeprom_ctx *ctx = (struct mconf_zephyr_eeprom_ctx *)callback_ctx;
    uint8_t ff_buf[MCONF_EEPROM_ERASE_CHUNK_SIZE];
    size_t remaining = size;
    size_t current_offset = offset;

    if (ctx == NULL) {
        return -1;
    }
    if ((offset + size) > ctx->total_size) {
        return -1;
    }

    memset(ff_buf, 0xFF, sizeof(ff_buf));

    while (remaining > 0u) {
        size_t chunk = (remaining > sizeof(ff_buf)) ? sizeof(ff_buf) : remaining;
        off_t target = (off_t)(ctx->base_offset + current_offset);
        int rc = eeprom_write(ctx->dev, target, ff_buf, chunk);
        if (rc < 0) {
            LOG_ERR("eeprom_write erase failed at offset 0x%" PRIxPTR ": %d", (uintptr_t)target, rc);
            return -1;
        }
        remaining -= chunk;
        current_offset += chunk;
    }

    return 0;
}

int mconf_zephyr_eeprom_init(struct mconf_zephyr_eeprom_ctx *ctx,
                             const struct device *dev,
                             size_t base_offset,
                             size_t total_size,
                             mconf_io_t *io)
{
    size_t dev_size;

    if ((ctx == NULL) || (dev == NULL) || (io == NULL)) {
        return -EINVAL;
    }

    if (!device_is_ready(dev)) {
        LOG_ERR("EEPROM device '%s' is not ready", device_name(dev));
        return -ENODEV;
    }

    dev_size = eeprom_get_size(dev);
    if (dev_size == 0u) {
        LOG_ERR("EEPROM device '%s' reports zero capacity", device_name(dev));
        return -ENODEV;
    }

    if (base_offset >= dev_size) {
        LOG_ERR("base_offset (%zu) exceeds EEPROM size (%zu)", base_offset, dev_size);
        return -EINVAL;
    }

    if (total_size == 0u) {
        total_size = dev_size - base_offset;
    }

    if ((base_offset + total_size) > dev_size) {
        LOG_ERR("Requested region (%zu + %zu) exceeds EEPROM size (%zu)",
                base_offset, total_size, dev_size);
        return -EINVAL;
    }

    /* Each slot must at least hold the 32-byte header, total_size must be even */
    if ((total_size < 64u) || ((total_size % 2u) != 0u)) {
        LOG_ERR("total_size (%zu) must be even and >= 64 bytes", total_size);
        return -EINVAL;
    }

    ctx->dev = dev;
    ctx->base_offset = base_offset;
    ctx->total_size = total_size;

    io->callback_ctx = ctx;
    io->storage_size = total_size;
    io->slot_size = total_size / 2u;
    io->read = mconf_zephyr_eeprom_read;
    io->write = mconf_zephyr_eeprom_write;
    io->erase = mconf_zephyr_eeprom_erase;

    LOG_INF("EEPROM backend initialized: %zu bytes total, 2 slots of %zu bytes on '%s'",
            total_size, io->slot_size, device_name(dev));

    return 0;
}
