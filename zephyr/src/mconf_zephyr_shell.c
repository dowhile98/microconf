/*
 * microconf Zephyr Shell Commands
 *
 * SPDX-License-Identifier: MIT
 */

#include "mconf_zephyr.h"

#include <inttypes.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MCONF_SHELL_MAX_INSTANCES 4

struct mconf_shell_entry {
    mconf_t *ctx;
    const mconf_io_t *io;
    char name[24];
    bool in_use;
};

static struct mconf_shell_entry s_entries[MCONF_SHELL_MAX_INSTANCES];
static struct k_mutex s_shell_mutex;
static bool s_mutex_initialized;

static void shell_mutex_lock(void)
{
    if (!s_mutex_initialized) {
        k_mutex_init(&s_shell_mutex);
        s_mutex_initialized = true;
    }
    k_mutex_lock(&s_shell_mutex, K_FOREVER);
}

static void shell_mutex_unlock(void)
{
    if (s_mutex_initialized) {
        k_mutex_unlock(&s_shell_mutex);
    }
}

int mconf_zephyr_shell_register(mconf_t *ctx, const mconf_io_t *io, const char *name)
{
    size_t i;
    int ret;

    if ((ctx == NULL) || (name == NULL) || (name[0] == '\0')) {
        return -EINVAL;
    }

    shell_mutex_lock();

    for (i = 0; i < MCONF_SHELL_MAX_INSTANCES; ++i) {
        if (s_entries[i].in_use && (strcmp(s_entries[i].name, name) == 0)) {
            s_entries[i].ctx = ctx;
            s_entries[i].io = io;
            ret = 0;
            goto unlock;
        }
    }

    for (i = 0; i < MCONF_SHELL_MAX_INSTANCES; ++i) {
        if (!s_entries[i].in_use) {
            s_entries[i].ctx = ctx;
            s_entries[i].io = io;
            strncpy(s_entries[i].name, name, sizeof(s_entries[i].name) - 1u);
            s_entries[i].name[sizeof(s_entries[i].name) - 1u] = '\0';
            s_entries[i].in_use = true;
            ret = 0;
            goto unlock;
        }
    }

    ret = -ENOMEM;
unlock:
    shell_mutex_unlock();
    return ret;
}

static struct mconf_shell_entry *find_shell_entry(const char *name)
{
    size_t i;
    for (i = 0; i < MCONF_SHELL_MAX_INSTANCES; ++i) {
        if (s_entries[i].in_use && (strcmp(s_entries[i].name, name) == 0)) {
            return &s_entries[i];
        }
    }
    return NULL;
}

/* Caller must hold s_shell_mutex */
static struct mconf_shell_entry *find_shell_entry_locked(const char *name)
{
    size_t i;
    for (i = 0; i < MCONF_SHELL_MAX_INSTANCES; ++i) {
        if (s_entries[i].in_use && (strcmp(s_entries[i].name, name) == 0)) {
            return &s_entries[i];
        }
    }
    return NULL;
}

static void print_field_value(const struct shell *sh, const mconf_t *ctx, size_t index)
{
    const mconf_entry_t *e = &ctx->schema->entries[index];
    const uint8_t *data = (const uint8_t *)ctx->data + e->offset;

    switch (e->type) {
    case MCONF_TYPE_BOOL:
        shell_print(sh, "%s", (*data != 0u) ? "true" : "false");
        break;
    case MCONF_TYPE_U8:
        shell_print(sh, "%u", (unsigned int)*data);
        break;
    case MCONF_TYPE_I8:
        shell_print(sh, "%d", (int)(*(const int8_t *)data));
        break;
    case MCONF_TYPE_U16: {
        uint16_t val;
        memcpy(&val, data, sizeof(val));
        shell_print(sh, "%u", (unsigned int)val);
        break;
    }
    case MCONF_TYPE_I16: {
        int16_t val;
        memcpy(&val, data, sizeof(val));
        shell_print(sh, "%d", (int)val);
        break;
    }
    case MCONF_TYPE_U32: {
        uint32_t val;
        memcpy(&val, data, sizeof(val));
        shell_print(sh, "%u", (unsigned int)val);
        break;
    }
    case MCONF_TYPE_I32: {
        int32_t val;
        memcpy(&val, data, sizeof(val));
        shell_print(sh, "%" PRId32, val);
        break;
    }
#if (MCONF_ENABLE_FLOAT != 0)
    case MCONF_TYPE_FLOAT32: {
        float val;
        memcpy(&val, data, sizeof(val));
        shell_print(sh, "%f", (double)val);
        break;
    }
#endif
    case MCONF_TYPE_STRING:
        shell_print(sh, "\"%s\"", (const char *)data);
        break;
    case MCONF_TYPE_BLOB: {
        size_t b;
        shell_fprintf(sh, SHELL_NORMAL, "[hex: ");
        for (b = 0; b < e->size; ++b) {
            shell_fprintf(sh, SHELL_NORMAL, "%02x%s", data[b], (b + 1u < e->size) ? " " : "");
        }
        shell_print(sh, "]");
        break;
    }
    default:
        shell_print(sh, "(unsupported type)");
        break;
    }
}

static int cmd_mconf_list(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    size_t i;
    size_t count = 0;

    shell_mutex_lock();
    shell_print(sh, "Registered microconf configurations:");
    for (i = 0; i < MCONF_SHELL_MAX_INSTANCES; ++i) {
        if (s_entries[i].in_use) {
            shell_print(sh, "  [%s] - %zu schema entries, %zu bytes data, persistence: %s",
                        s_entries[i].name,
                        s_entries[i].ctx->schema->entry_count,
                        s_entries[i].ctx->data_size,
                        s_entries[i].io != NULL ? "yes" : "none");
            count++;
        }
    }
    shell_mutex_unlock();
    if (count == 0) {
        shell_print(sh, "  (none)");
    }
    return 0;
}

static int cmd_mconf_dump(const struct shell *sh, size_t argc, char **argv)
{
    struct mconf_shell_entry *entry;
    size_t i;

    if (argc < 2) {
        shell_error(sh, "Usage: mconf dump <name>");
        return -EINVAL;
    }

    shell_mutex_lock();
    entry = find_shell_entry_locked(argv[1]);
    if (entry == NULL) {
        shell_mutex_unlock();
        shell_error(sh, "Configuration '%s' not found", argv[1]);
        return -ENOENT;
    }

    shell_print(sh, "=== Schema dump: %s (v%u, %zu entries) ===",
                entry->name, entry->ctx->schema->schema_version,
                entry->ctx->schema->entry_count);

    for (i = 0; i < entry->ctx->schema->entry_count; ++i) {
        const mconf_entry_t *e = &entry->ctx->schema->entries[i];
        const char *key = (e->key != NULL) ? e->key : "(unnamed)";
        shell_fprintf(sh, SHELL_NORMAL, "  [%2zu] %-24s (%-7s, %2zuB): ",
                      i, key, mconf_type_name(e->type), e->size);
        print_field_value(sh, entry->ctx, i);
    }

    shell_mutex_unlock();
    return 0;
}

static int cmd_mconf_get(const struct shell *sh, size_t argc, char **argv)
{
    struct mconf_shell_entry *entry;
    size_t index;
    mconf_err_t err;

    if (argc < 3) {
        shell_error(sh, "Usage: mconf get <name> <key>");
        return -EINVAL;
    }

    shell_mutex_lock();
    entry = find_shell_entry_locked(argv[1]);
    if (entry == NULL) {
        shell_mutex_unlock();
        shell_error(sh, "Configuration '%s' not found", argv[1]);
        return -ENOENT;
    }

    err = mconf_find(entry->ctx, argv[2], &index);
    if (err != MCONF_OK) {
        shell_mutex_unlock();
        shell_error(sh, "Key '%s' not found in '%s'", argv[2], argv[1]);
        return -ENOENT;
    }

    print_field_value(sh, entry->ctx, index);
    shell_mutex_unlock();
    return 0;
}

static int cmd_mconf_set(const struct shell *sh, size_t argc, char **argv)
{
    struct mconf_shell_entry *entry;
    size_t index;
    mconf_err_t err;
    const mconf_entry_t *e;

    if (argc < 4) {
        shell_error(sh, "Usage: mconf set <name> <key> <value>");
        return -EINVAL;
    }

    shell_mutex_lock();
    entry = find_shell_entry_locked(argv[1]);
    if (entry == NULL) {
        shell_mutex_unlock();
        shell_error(sh, "Configuration '%s' not found", argv[1]);
        return -ENOENT;
    }

    err = mconf_find(entry->ctx, argv[2], &index);
    if (err != MCONF_OK) {
        shell_mutex_unlock();
        shell_error(sh, "Key '%s' not found", argv[2]);
        return -ENOENT;
    }

    e = &entry->ctx->schema->entries[index];

    switch (e->type) {
    case MCONF_TYPE_BOOL: {
        bool b = (strcmp(argv[3], "1") == 0) || (strcasecmp(argv[3], "true") == 0);
        err = mconf_set_bool(entry->ctx, index, b);
        break;
    }
    case MCONF_TYPE_U8: {
        uint8_t val = (uint8_t)strtoul(argv[3], NULL, 0);
        err = mconf_set_u8(entry->ctx, index, val);
        break;
    }
    case MCONF_TYPE_U16: {
        uint16_t val = (uint16_t)strtoul(argv[3], NULL, 0);
        err = mconf_set_u16(entry->ctx, index, val);
        break;
    }
    case MCONF_TYPE_U32: {
        uint32_t val = (uint32_t)strtoul(argv[3], NULL, 0);
        err = mconf_set_u32(entry->ctx, index, val);
        break;
    }
    case MCONF_TYPE_I32: {
        int32_t val = (int32_t)strtol(argv[3], NULL, 0);
        err = mconf_set_i32(entry->ctx, index, val);
        break;
    }
#if (MCONF_ENABLE_FLOAT != 0)
    case MCONF_TYPE_FLOAT32: {
        float val = strtof(argv[3], NULL);
        err = mconf_set_float(entry->ctx, index, val);
        break;
    }
#endif
    case MCONF_TYPE_STRING: {
        size_t required = 0;
        err = mconf_set_string(entry->ctx, index, argv[3], strlen(argv[3]), &required);
        break;
    }
    default:
        shell_mutex_unlock();
        shell_error(sh, "Type '%s' not editable via simple shell string", mconf_type_name(e->type));
        return -ENOTSUP;
    }

    if (err != MCONF_OK) {
        shell_mutex_unlock();
        shell_error(sh, "Failed to set field: %s", mconf_err_str(err));
        return -EINVAL;
    }

    shell_mutex_unlock();
    shell_print(sh, "Key '%s' updated successfully.", argv[2]);
    return 0;
}

static int cmd_mconf_save(const struct shell *sh, size_t argc, char **argv)
{
    struct mconf_shell_entry *entry;
    mconf_err_t err;

    if (argc < 2) {
        shell_error(sh, "Usage: mconf save <name>");
        return -EINVAL;
    }

    shell_mutex_lock();
    entry = find_shell_entry_locked(argv[1]);
    if (entry == NULL) {
        shell_mutex_unlock();
        shell_error(sh, "Configuration '%s' not found", argv[1]);
        return -ENOENT;
    }
    if (entry->io == NULL) {
        shell_mutex_unlock();
        shell_error(sh, "Configuration '%s' has no persistent IO backend attached", argv[1]);
        return -ENODEV;
    }

    err = mconf_save(entry->ctx, entry->io);
    shell_mutex_unlock();
    if (err != MCONF_OK) {
        shell_error(sh, "Failed to save: %s (%d)", mconf_err_str(err), (int)err);
        return -EIO;
    }

    shell_print(sh, "Configuration '%s' saved to persistent storage.", argv[1]);
    return 0;
}

static int cmd_mconf_load(const struct shell *sh, size_t argc, char **argv)
{
    struct mconf_shell_entry *entry;
    mconf_err_t err;

    if (argc < 2) {
        shell_error(sh, "Usage: mconf load <name>");
        return -EINVAL;
    }

    shell_mutex_lock();
    entry = find_shell_entry_locked(argv[1]);
    if (entry == NULL) {
        shell_mutex_unlock();
        shell_error(sh, "Configuration '%s' not found", argv[1]);
        return -ENOENT;
    }
    if (entry->io == NULL) {
        shell_mutex_unlock();
        shell_error(sh, "Configuration '%s' has no persistent IO backend attached", argv[1]);
        return -ENODEV;
    }

    err = mconf_load(entry->ctx, entry->io);
    shell_mutex_unlock();
    if (err != MCONF_OK) {
        shell_error(sh, "Failed to load: %s (%d)", mconf_err_str(err), (int)err);
        return -EIO;
    }

    shell_print(sh, "Configuration '%s' loaded from persistent storage.", argv[1]);
    return 0;
}

static int cmd_mconf_defaults(const struct shell *sh, size_t argc, char **argv)
{
    struct mconf_shell_entry *entry;
    mconf_err_t err;

    if (argc < 2) {
        shell_error(sh, "Usage: mconf defaults <name>");
        return -EINVAL;
    }

    shell_mutex_lock();
    entry = find_shell_entry_locked(argv[1]);
    if (entry == NULL) {
        shell_mutex_unlock();
        shell_error(sh, "Configuration '%s' not found", argv[1]);
        return -ENOENT;
    }

    err = mconf_load_defaults(entry->ctx);
    shell_mutex_unlock();
    if (err != MCONF_OK) {
        shell_error(sh, "Failed to load defaults: %s", mconf_err_str(err));
        return -EINVAL;
    }

    shell_print(sh, "Configuration '%s' reset to schema defaults.", argv[1]);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_mconf,
    SHELL_CMD_ARG(list, NULL, "List registered configurations", cmd_mconf_list, 1, 0),
    SHELL_CMD_ARG(dump, NULL, "Dump all keys and values: mconf dump <name>", cmd_mconf_dump, 2, 0),
    SHELL_CMD_ARG(get, NULL, "Get a key value: mconf get <name> <key>", cmd_mconf_get, 3, 0),
    SHELL_CMD_ARG(set, NULL, "Set a key value: mconf set <name> <key> <val>", cmd_mconf_set, 4, 0),
    SHELL_CMD_ARG(save, NULL, "Save to persistent storage: mconf save <name>", cmd_mconf_save, 2, 0),
    SHELL_CMD_ARG(load, NULL, "Load from persistent storage: mconf load <name>", cmd_mconf_load, 2, 0),
    SHELL_CMD_ARG(defaults, NULL, "Reset to schema defaults: mconf defaults <name>", cmd_mconf_defaults, 2, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(mconf, &sub_mconf, "microconf configuration commands", NULL);
