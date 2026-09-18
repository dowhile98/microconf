/*
 * microconf Zephyr Flash Map Storage Backend
 *
 * SPDX-License-Identifier: MIT
 */

#include "mconf_zephyr.h"

#include <inttypes.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mconf_flash, CONFIG_MICROCONF_LOG_LEVEL);

static int mconf_zephyr_flash_read(void *callback_ctx, size_t offset, void *buffer, size_t size)
{
    struct mconf_zephyr_flash_ctx *ctx = (struct mconf_zephyr_flash_ctx *)callback_ctx;
    int rc;

    if ((ctx == NULL) || (ctx->fa == NULL) || (buffer == NULL)) {
        return -1;
    }
    if ((offset + size) > ctx->total_size) {
        return -1;
    }

    rc = flash_area_read(ctx->fa, (off_t)(ctx->base_offset + offset), buffer, size);
    if (rc != 0) {
        LOG_ERR("flash_area_read failed at offset 0x%" PRIxPTR " (size %zu): %d",
                (uintptr_t)(ctx->base_offset + offset), size, rc);
        return -1;
    }
    return 0;
}

static int mconf_zephyr_flash_write(void *callback_ctx, size_t offset, const void *buffer, size_t size)
{
    struct mconf_zephyr_flash_ctx *ctx = (struct mconf_zephyr_flash_ctx *)callback_ctx;
    int rc;

    if ((ctx == NULL) || (ctx->fa == NULL) || (buffer == NULL)) {
        return -1;
    }
    if ((offset + size) > ctx->total_size) {
        return -1;
    }

    rc = flash_area_write(ctx->fa, (off_t)(ctx->base_offset + offset), buffer, size);
    if (rc != 0) {
        LOG_ERR("flash_area_write failed at offset 0x%" PRIxPTR " (size %zu): %d",
                (uintptr_t)(ctx->base_offset + offset), size, rc);
        return -1;
    }
    return 0;
}

static int mconf_zephyr_flash_erase(void *callback_ctx, size_t offset, size_t size)
{
    struct mconf_zephyr_flash_ctx *ctx = (struct mconf_zephyr_flash_ctx *)callback_ctx;
    int rc;

    if ((ctx == NULL) || (ctx->fa == NULL)) {
        return -1;
    }
    if ((offset + size) > ctx->total_size) {
        return -1;
    }

    rc = flash_area_erase(ctx->fa, (off_t)(ctx->base_offset + offset), size);
    if (rc != 0) {
        LOG_ERR("flash_area_erase failed at offset 0x%" PRIxPTR " (size %zu): %d",
                (uintptr_t)(ctx->base_offset + offset), size, rc);
        return -1;
    }
    return 0;
}

int mconf_zephyr_flash_init(struct mconf_zephyr_flash_ctx *ctx,
                            uint8_t area_id,
                            mconf_io_t *io)
{
    size_t area_size;
    int rc;

    if ((ctx == NULL) || (io == NULL)) {
        return -EINVAL;
    }

    rc = flash_area_open(area_id, &ctx->fa);
    if (rc != 0) {
        LOG_ERR("flash_area_open failed for area ID %u: %d", area_id, rc);
        return rc;
    }

    area_size = flash_area_get_size(ctx->fa);
    if ((area_size < 64u) || ((area_size % 2u) != 0u)) {
        LOG_ERR("Flash area %u size (%zu) must be even and >= 64 bytes", area_id, area_size);
        flash_area_close(ctx->fa);
        ctx->fa = NULL;
        return -EINVAL;
    }

    ctx->area_id = area_id;
    ctx->base_offset = 0u;
    ctx->total_size = area_size;

    io->callback_ctx = ctx;
    io->storage_size = area_size;
    io->slot_size = area_size / 2u;
    io->read = mconf_zephyr_flash_read;
    io->write = mconf_zephyr_flash_write;
    io->erase = mconf_zephyr_flash_erase;

    LOG_INF("Flash backend initialized: %zu bytes total, 2 slots of %zu bytes on area %u",
            area_size, io->slot_size, area_id);

    return 0;
}

void mconf_zephyr_flash_close(struct mconf_zephyr_flash_ctx *ctx)
{
    if ((ctx != NULL) && (ctx->fa != NULL)) {
        flash_area_close(ctx->fa);
        ctx->fa = NULL;
    }
}
