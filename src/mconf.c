/*
 * microconf runtime
 *
 * SPDX-License-Identifier: MIT
 */

#include "mconf.h"

#if (MCONF_ENABLE_FLOAT != 0)
#include <float.h>
#endif
#include <string.h>

#define MCONF_STORAGE_MAGIC_0 ((uint8_t)'M')
#define MCONF_STORAGE_MAGIC_1 ((uint8_t)'C')
#define MCONF_STORAGE_MAGIC_2 ((uint8_t)'F')
#define MCONF_STORAGE_MAGIC_3 ((uint8_t)'G')
#define MCONF_STORAGE_STATE_UNCOMMITTED ((uint8_t)0x3Cu)
#define MCONF_STORAGE_STATE_COMMITTED   ((uint8_t)0xA5u)
#define MCONF_STORAGE_HEADER_SIZE ((size_t)32u)
#define MCONF_STORAGE_STATE_OFFSET ((size_t)18u)
#define MCONF_U16_MAX_VALUE ((size_t)0xFFFFu)

typedef struct {
    uint32_t schema_fingerprint;
    uint32_t schema_version;
    uint32_t generation;
    uint32_t payload_length;
    uint32_t payload_crc32;
    uint16_t entry_count;
    uint16_t format_version;
    uint8_t state;
} mconf_record_info_t;

static int mconf_is_little_endian_ieee754(void)
{
#if (MCONF_ENABLE_FLOAT == 0)
    return 0;
#else
    if ((sizeof(float) != 4u) || (FLT_RADIX != 2) || (FLT_MANT_DIG != 24) || (FLT_MAX_EXP != 128)) {
        return 0;
    }
    {
        const uint32_t le_test = 0x01020304u;
        const uint8_t *bytes = (const uint8_t *)&le_test;
        if (bytes[0] != 0x04u) {
            return 0;
        }
    }
    return 1;
#endif
}

static void mconf_write_u16_le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void mconf_write_u32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint16_t mconf_read_u16_le(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t mconf_read_u32_le(const uint8_t *src)
{
    return ((uint32_t)src[0]) |
        ((uint32_t)src[1] << 8) |
        ((uint32_t)src[2] << 16) |
        ((uint32_t)src[3] << 24);
}

static void mconf_crc32_update_byte(uint32_t *crc, uint8_t byte)
{
    uint32_t value = *crc ^ (uint32_t)byte;
    int bit_index;
    for (bit_index = 0; bit_index < 8; ++bit_index) {
        if ((value & 1u) != 0u) {
            value = (value >> 1) ^ 0xEDB88320u;
        } else {
            value >>= 1;
        }
    }
    *crc = value;
}

static uint32_t mconf_crc32_finalize(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

uint32_t mconf_crc32(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;
    uint32_t crc = 0xFFFFFFFFu;

    for (index = 0; index < size; ++index) {
        mconf_crc32_update_byte(&crc, bytes[index]);
    }

    return mconf_crc32_finalize(crc);
}

static uint32_t mconf_hash_mix(uint32_t hash, uint8_t byte)
{
    return (hash ^ (uint32_t)byte) * 16777619u;
}

static uint32_t mconf_schema_hash_bytes(uint32_t hash, const uint8_t *bytes, size_t size)
{
    size_t index;
    for (index = 0; index < size; ++index) {
        hash = mconf_hash_mix(hash, bytes[index]);
    }
    return hash;
}

static uint32_t mconf_schema_hash_size(uint32_t hash, size_t value)
{
    uint8_t bytes[8];
    size_t index;
    for (index = 0; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)((value >> (index * 8u)) & 0xFFu);
    }
    return mconf_schema_hash_bytes(hash, bytes, sizeof(bytes));
}

static const mconf_entry_t *mconf_entry_at(const mconf_t *ctx, size_t index)
{
    return &ctx->schema->entries[index];
}

static void *mconf_field_ptr(void *data, const mconf_entry_t *entry)
{
    return (uint8_t *)data + entry->offset;
}

static const void *mconf_field_ptr_const(const void *data, const mconf_entry_t *entry)
{
    return (const uint8_t *)data + entry->offset;
}

static mconf_err_t mconf_require_context(const mconf_t *ctx)
{
    if (ctx == NULL) {
        return MCONF_ERR_NULL;
    }
    if ((!ctx->initialized) || (ctx->schema == NULL) || (ctx->data == NULL)) {
        return MCONF_ERR_UNINITIALIZED;
    }
    return MCONF_OK;
}

static mconf_err_t mconf_check_index(const mconf_t *ctx, size_t index)
{
    mconf_err_t err = mconf_require_context(ctx);
    if (err != MCONF_OK) {
        return err;
    }
    if (index >= ctx->schema->entry_count) {
        return MCONF_ERR_NOT_FOUND;
    }
    return MCONF_OK;
}

static bool mconf_is_scalar_type(mconf_type_t type)
{
    return (type >= MCONF_TYPE_BOOL) && (type <= MCONF_TYPE_FLOAT32);
}

static size_t mconf_expected_type_size(mconf_type_t type)
{
    switch (type) {
    case MCONF_TYPE_BOOL:
    case MCONF_TYPE_U8:
    case MCONF_TYPE_I8:
        return 1u;
    case MCONF_TYPE_U16:
    case MCONF_TYPE_I16:
        return 2u;
    case MCONF_TYPE_U32:
    case MCONF_TYPE_I32:
    case MCONF_TYPE_FLOAT32:
        return 4u;
    default:
        return 0u;
    }
}

static mconf_err_t mconf_validate_string_bytes(const uint8_t *bytes, size_t size, size_t *length_out)
{
    size_t index;
    if (size < 2u) {
        return MCONF_ERR_INVALID;
    }
    for (index = 0; index < size; ++index) {
        if (bytes[index] == 0u) {
            if (length_out != NULL) {
                *length_out = index;
            }
            return MCONF_OK;
        }
    }
    return MCONF_ERR_INVALID;
}

static mconf_err_t mconf_validate_default_for_entry(const mconf_entry_t *entry)
{
    if (entry->default_value == NULL) {
        return (entry->default_size == 0u) ? MCONF_OK : MCONF_ERR_INVALID;
    }

    if (entry->type == MCONF_TYPE_STRING) {
        if ((entry->default_size >= entry->size) || (entry->size < 2u)) {
            return MCONF_ERR_INVALID;
        }
        return MCONF_OK;
    }

    if (entry->type == MCONF_TYPE_BLOB) {
        return (entry->default_size == entry->size) ? MCONF_OK : MCONF_ERR_INVALID;
    }

    if (mconf_is_scalar_type(entry->type)) {
        return (entry->default_size == entry->size) ? MCONF_OK : MCONF_ERR_INVALID;
    }

    return MCONF_ERR_INVALID;
}

static void mconf_store_float_bits(float value, uint8_t *bytes)
{
    union {
        float as_float;
        uint32_t as_u32;
    } bits;
    bits.as_float = value;
    mconf_write_u32_le(bytes, bits.as_u32);
}

static void mconf_load_float_bits(const uint8_t *bytes, float *value_out)
{
    union {
        float as_float;
        uint32_t as_u32;
    } bits;
    bits.as_u32 = mconf_read_u32_le(bytes);
    *value_out = bits.as_float;
}

static mconf_err_t mconf_encode_field_bytes(const mconf_entry_t *entry, const void *field, uint8_t *buffer, size_t *encoded_size_out)
{
    switch (entry->type) {
    case MCONF_TYPE_BOOL:
    case MCONF_TYPE_U8:
    case MCONF_TYPE_I8:
        buffer[0] = ((const uint8_t *)field)[0];
        *encoded_size_out = 1u;
        return MCONF_OK;
    case MCONF_TYPE_U16:
    case MCONF_TYPE_I16:
        mconf_write_u16_le(buffer, *(const uint16_t *)field);
        *encoded_size_out = 2u;
        return MCONF_OK;
    case MCONF_TYPE_U32:
    case MCONF_TYPE_I32:
        mconf_write_u32_le(buffer, *(const uint32_t *)field);
        *encoded_size_out = 4u;
        return MCONF_OK;
    case MCONF_TYPE_FLOAT32:
        if ((MCONF_ENABLE_FLOAT == 0) || (!mconf_is_little_endian_ieee754())) {
            return MCONF_ERR_UNSUPPORTED;
        }
        mconf_store_float_bits(*(const float *)field, buffer);
        *encoded_size_out = 4u;
        return MCONF_OK;
    case MCONF_TYPE_BLOB:
        memcpy(buffer, field, entry->size);
        *encoded_size_out = entry->size;
        return MCONF_OK;
    default:
        return MCONF_ERR_TYPE;
    }
}

static mconf_err_t mconf_decode_field_bytes(const mconf_entry_t *entry, const uint8_t *buffer, size_t encoded_size, void *field_out)
{
    switch (entry->type) {
    case MCONF_TYPE_BOOL:
    case MCONF_TYPE_U8:
    case MCONF_TYPE_I8:
        if (encoded_size != 1u) {
            return MCONF_ERR_SIZE;
        }
        ((uint8_t *)field_out)[0] = buffer[0];
        return MCONF_OK;
    case MCONF_TYPE_U16:
    case MCONF_TYPE_I16:
        if (encoded_size != 2u) {
            return MCONF_ERR_SIZE;
        }
        *(uint16_t *)field_out = mconf_read_u16_le(buffer);
        return MCONF_OK;
    case MCONF_TYPE_U32:
    case MCONF_TYPE_I32:
        if (encoded_size != 4u) {
            return MCONF_ERR_SIZE;
        }
        *(uint32_t *)field_out = mconf_read_u32_le(buffer);
        return MCONF_OK;
    case MCONF_TYPE_FLOAT32:
        if (encoded_size != 4u) {
            return MCONF_ERR_SIZE;
        }
        if ((MCONF_ENABLE_FLOAT == 0) || (!mconf_is_little_endian_ieee754())) {
            return MCONF_ERR_UNSUPPORTED;
        }
        mconf_load_float_bits(buffer, (float *)field_out);
        return MCONF_OK;
    case MCONF_TYPE_BLOB:
        if (encoded_size != entry->size) {
            return MCONF_ERR_SIZE;
        }
        memcpy(field_out, buffer, entry->size);
        return MCONF_OK;
    default:
        return MCONF_ERR_TYPE;
    }
}

static mconf_err_t mconf_apply_default_to_field(uint8_t *dst, const mconf_entry_t *entry)
{
    if (entry->default_value == NULL) {
        memset(dst, 0, entry->size);
        return MCONF_OK;
    }

    if (entry->type == MCONF_TYPE_STRING) {
        memset(dst, 0, entry->size);
        memcpy(dst, entry->default_value, entry->default_size);
        dst[entry->default_size] = 0u;
        return MCONF_OK;
    }

    memcpy(dst, entry->default_value, entry->size);
    return MCONF_OK;
}

static mconf_err_t mconf_validate_entry_layout(const mconf_schema_t *schema, size_t index)
{
    const mconf_entry_t *entry = &schema->entries[index];
    size_t expected_size;
    size_t end_offset;
    size_t other_index;

    if ((entry->key != NULL) && (strlen(entry->key) > MCONF_MAX_KEY_LEN)) {
        return MCONF_ERR_INVALID;
    }
    if ((entry->size == 0u) || (entry->offset > schema->data_size)) {
        return MCONF_ERR_INVALID;
    }
    if ((entry->offset + entry->size) < entry->offset) {
        return MCONF_ERR_INVALID;
    }
    end_offset = entry->offset + entry->size;
    if (end_offset > schema->data_size) {
        return MCONF_ERR_INVALID;
    }

    expected_size = mconf_expected_type_size(entry->type);
    if ((expected_size != 0u) && (entry->size != expected_size)) {
        return MCONF_ERR_INVALID;
    }
    if ((entry->type == MCONF_TYPE_STRING) && (entry->size < 2u)) {
        return MCONF_ERR_INVALID;
    }
    if ((entry->type == MCONF_TYPE_FLOAT32) && (MCONF_ENABLE_FLOAT == 0)) {
        return MCONF_ERR_INVALID;
    }

    if (mconf_validate_default_for_entry(entry) != MCONF_OK) {
        return MCONF_ERR_INVALID;
    }

    for (other_index = index + 1u; other_index < schema->entry_count; ++other_index) {
        const mconf_entry_t *other = &schema->entries[other_index];
        size_t other_end = other->offset + other->size;
        if ((entry->offset < other_end) && (other->offset < end_offset)) {
            return MCONF_ERR_INVALID;
        }
        if ((entry->key != NULL) && (other->key != NULL) && (strcmp(entry->key, other->key) == 0)) {
            return MCONF_ERR_INVALID;
        }
    }

    return MCONF_OK;
}

const char *mconf_err_str(mconf_err_t err)
{
    switch (err) {
    case MCONF_OK: return "ok";
    case MCONF_ERR_NULL: return "null argument";
    case MCONF_ERR_NOT_FOUND: return "not found";
    case MCONF_ERR_TYPE: return "type mismatch";
    case MCONF_ERR_SIZE: return "size mismatch";
    case MCONF_ERR_CRC: return "crc mismatch";
    case MCONF_ERR_MAGIC: return "invalid magic";
    case MCONF_ERR_VERSION: return "version mismatch";
    case MCONF_ERR_IO: return "io failure";
    case MCONF_ERR_INVALID: return "invalid state";
    case MCONF_ERR_RANGE: return "range error";
    case MCONF_ERR_UNSUPPORTED: return "unsupported";
    case MCONF_ERR_UNINITIALIZED: return "uninitialized";
    case MCONF_ERR_CONFLICT: return "conflict";
    default: return "unknown";
    }
}

const char *mconf_type_name(mconf_type_t type)
{
    switch (type) {
    case MCONF_TYPE_BOOL: return "bool";
    case MCONF_TYPE_U8: return "u8";
    case MCONF_TYPE_I8: return "i8";
    case MCONF_TYPE_U16: return "u16";
    case MCONF_TYPE_I16: return "i16";
    case MCONF_TYPE_U32: return "u32";
    case MCONF_TYPE_I32: return "i32";
    case MCONF_TYPE_FLOAT32: return "float32";
    case MCONF_TYPE_STRING: return "string";
    case MCONF_TYPE_BLOB: return "blob";
    default: return "unknown";
    }
}

mconf_err_t mconf_schema_validate(const mconf_schema_t *schema, uint32_t *fingerprint_out)
{
    size_t index;
    uint32_t hash = 2166136261u;

    if ((schema == NULL) || (schema->entries == NULL)) {
        return MCONF_ERR_NULL;
    }
    if ((schema->entry_count == 0u) || (schema->entry_count > MCONF_MAX_ENTRIES) || (schema->data_size == 0u)) {
        return MCONF_ERR_INVALID;
    }

    hash = mconf_schema_hash_size(hash, schema->entry_count);
    hash = mconf_schema_hash_size(hash, schema->data_size);
    hash = mconf_schema_hash_size(hash, schema->schema_version);

    for (index = 0; index < schema->entry_count; ++index) {
        const mconf_entry_t *entry = &schema->entries[index];
        mconf_err_t entry_err = mconf_validate_entry_layout(schema, index);
        if (entry_err != MCONF_OK) {
            return entry_err;
        }
        hash = mconf_schema_hash_size(hash, entry->offset);
        hash = mconf_schema_hash_size(hash, entry->size);
        hash = mconf_schema_hash_size(hash, entry->type);
        if (entry->key != NULL) {
            hash = mconf_schema_hash_bytes(hash, (const uint8_t *)entry->key, strlen(entry->key));
        } else {
            hash = mconf_hash_mix(hash, 0u);
        }
    }

    if (fingerprint_out != NULL) {
        *fingerprint_out = hash;
    }
    return MCONF_OK;
}

mconf_err_t mconf_validate_data(const mconf_t *ctx, const void *data)
{
    size_t index;
    mconf_err_t ctx_err = mconf_require_context(ctx);
    if (ctx_err != MCONF_OK) {
        return ctx_err;
    }
    if (data == NULL) {
        return MCONF_ERR_NULL;
    }

    for (index = 0; index < ctx->schema->entry_count; ++index) {
        const mconf_entry_t *entry = mconf_entry_at(ctx, index);
        const uint8_t *bytes = (const uint8_t *)mconf_field_ptr_const(data, entry);

        if (entry->type == MCONF_TYPE_BOOL) {
            if ((bytes[0] != 0u) && (bytes[0] != 1u)) {
                return MCONF_ERR_INVALID;
            }
        } else if (entry->type == MCONF_TYPE_STRING) {
            if (mconf_validate_string_bytes(bytes, entry->size, NULL) != MCONF_OK) {
                return MCONF_ERR_INVALID;
            }
        } else if (entry->type == MCONF_TYPE_FLOAT32) {
            if ((MCONF_ENABLE_FLOAT == 0) || (!mconf_is_little_endian_ieee754())) {
                return MCONF_ERR_UNSUPPORTED;
            }
        }
    }

    return MCONF_OK;
}

mconf_err_t mconf_init(mconf_t *ctx, size_t ctx_size, const mconf_schema_t *schema, void *data, size_t data_size)
{
    uint32_t fingerprint = 0u;
    mconf_err_t schema_err;

    if ((ctx == NULL) || (schema == NULL) || (data == NULL)) {
        return MCONF_ERR_NULL;
    }
    if (ctx_size != sizeof(*ctx)) {
        return MCONF_ERR_SIZE;
    }
    if (data_size != schema->data_size) {
        return MCONF_ERR_SIZE;
    }

    schema_err = mconf_schema_validate(schema, &fingerprint);
    if (schema_err != MCONF_OK) {
        return schema_err;
    }

    ctx->schema = schema;
    ctx->data = data;
    ctx->data_size = data_size;
    ctx->schema_fingerprint = fingerprint;
    ctx->initialized = true;
    return MCONF_OK;
}

mconf_err_t mconf_load_defaults(mconf_t *ctx)
{
    size_t index;
    mconf_err_t err = mconf_require_context(ctx);
    if (err != MCONF_OK) {
        return err;
    }

    memset(ctx->data, 0, ctx->data_size);
    for (index = 0; index < ctx->schema->entry_count; ++index) {
        mconf_err_t default_err = mconf_apply_default_to_field((uint8_t *)mconf_field_ptr(ctx->data, mconf_entry_at(ctx, index)),
            mconf_entry_at(ctx, index));
        if (default_err != MCONF_OK) {
            return default_err;
        }
    }

    return MCONF_OK;
}

mconf_err_t mconf_reset_field(mconf_t *ctx, size_t index)
{
    mconf_err_t err = mconf_check_index(ctx, index);
    if (err != MCONF_OK) {
        return err;
    }
    return mconf_apply_default_to_field((uint8_t *)mconf_field_ptr(ctx->data, mconf_entry_at(ctx, index)),
        mconf_entry_at(ctx, index));
}

mconf_err_t mconf_find(const mconf_t *ctx, const char *key, size_t *index_out)
{
    size_t index;
    mconf_err_t err = mconf_require_context(ctx);
    if (err != MCONF_OK) {
        return err;
    }
    if ((key == NULL) || (index_out == NULL)) {
        return MCONF_ERR_NULL;
    }
#if (MCONF_ENABLE_NAMES == 0)
    (void)key;
    (void)index_out;
    return MCONF_ERR_UNSUPPORTED;
#endif

    for (index = 0; index < ctx->schema->entry_count; ++index) {
        const char *entry_key = mconf_entry_at(ctx, index)->key;
        if ((entry_key != NULL) && (strcmp(entry_key, key) == 0)) {
            *index_out = index;
            return MCONF_OK;
        }
    }
    return MCONF_ERR_NOT_FOUND;
}

static mconf_err_t mconf_copy_field_out(const mconf_entry_t *entry, const void *src, void *out, size_t out_size)
{
    if (out_size != entry->size) {
        return MCONF_ERR_SIZE;
    }
    memmove(out, src, entry->size);
    return MCONF_OK;
}

mconf_err_t mconf_get(const mconf_t *ctx, size_t index, void *out, size_t out_size)
{
    const mconf_entry_t *entry;
    mconf_err_t err = mconf_check_index(ctx, index);
    if (err != MCONF_OK) {
        return err;
    }
    if (out == NULL) {
        return MCONF_ERR_NULL;
    }
    entry = mconf_entry_at(ctx, index);
    return mconf_copy_field_out(entry, mconf_field_ptr_const(ctx->data, entry), out, out_size);
}

mconf_err_t mconf_set(mconf_t *ctx, size_t index, const void *value, size_t value_size)
{
    const mconf_entry_t *entry;
    void *dst;
    mconf_err_t err = mconf_check_index(ctx, index);
    if (err != MCONF_OK) {
        return err;
    }
    if (value == NULL) {
        return MCONF_ERR_NULL;
    }
    entry = mconf_entry_at(ctx, index);
    dst = mconf_field_ptr(ctx->data, entry);

    if (entry->type == MCONF_TYPE_STRING) {
        size_t required = 0u;
        return mconf_set_string(ctx, index, (const char *)value, value_size, &required);
    }
    if ((value_size == 0u) || (value_size != entry->size)) {
        return MCONF_ERR_SIZE;
    }
    if ((entry->type == MCONF_TYPE_BOOL) && ((((const uint8_t *)value)[0] != 0u) && (((const uint8_t *)value)[0] != 1u))) {
        return MCONF_ERR_INVALID;
    }
    memmove(dst, value, entry->size);
    return MCONF_OK;
}

static mconf_err_t mconf_get_typed_scalar(const mconf_t *ctx, size_t index, mconf_type_t expected_type, void *out, size_t out_size)
{
    const mconf_entry_t *entry;
    mconf_err_t err = mconf_check_index(ctx, index);
    if (err != MCONF_OK) {
        return err;
    }
    if (out == NULL) {
        return MCONF_ERR_NULL;
    }
    entry = mconf_entry_at(ctx, index);
    if (entry->type != expected_type) {
        return MCONF_ERR_TYPE;
    }
    if ((expected_type == MCONF_TYPE_FLOAT32) && ((MCONF_ENABLE_FLOAT == 0) || (!mconf_is_little_endian_ieee754()))) {
        return MCONF_ERR_UNSUPPORTED;
    }
    return mconf_copy_field_out(entry, mconf_field_ptr_const(ctx->data, entry), out, out_size);
}

static mconf_err_t mconf_set_typed_scalar(mconf_t *ctx, size_t index, mconf_type_t expected_type, const void *value, size_t value_size)
{
    const mconf_entry_t *entry;
    mconf_err_t err = mconf_check_index(ctx, index);
    if (err != MCONF_OK) {
        return err;
    }
    if (value == NULL) {
        return MCONF_ERR_NULL;
    }
    entry = mconf_entry_at(ctx, index);
    if (entry->type != expected_type) {
        return MCONF_ERR_TYPE;
    }
    if (value_size != entry->size) {
        return MCONF_ERR_SIZE;
    }
    if ((expected_type == MCONF_TYPE_FLOAT32) && ((MCONF_ENABLE_FLOAT == 0) || (!mconf_is_little_endian_ieee754()))) {
        return MCONF_ERR_UNSUPPORTED;
    }
    if ((expected_type == MCONF_TYPE_BOOL) && ((((const uint8_t *)value)[0] != 0u) && (((const uint8_t *)value)[0] != 1u))) {
        return MCONF_ERR_INVALID;
    }
    memmove(mconf_field_ptr(ctx->data, entry), value, entry->size);
    return MCONF_OK;
}

mconf_err_t mconf_get_bool(const mconf_t *ctx, size_t index, bool *out)
{
    uint8_t raw = 0u;
    mconf_err_t err = mconf_get_typed_scalar(ctx, index, MCONF_TYPE_BOOL, &raw, sizeof(raw));
    if (err != MCONF_OK) {
        return err;
    }
    if ((raw != 0u) && (raw != 1u)) {
        return MCONF_ERR_INVALID;
    }
    *out = (raw != 0u);
    return MCONF_OK;
}

mconf_err_t mconf_set_bool(mconf_t *ctx, size_t index, bool value)
{
    uint8_t raw = value ? 1u : 0u;
    return mconf_set_typed_scalar(ctx, index, MCONF_TYPE_BOOL, &raw, sizeof(raw));
}

mconf_err_t mconf_get_u8(const mconf_t *ctx, size_t index, uint8_t *out) { return mconf_get_typed_scalar(ctx, index, MCONF_TYPE_U8, out, sizeof(*out)); }
mconf_err_t mconf_set_u8(mconf_t *ctx, size_t index, uint8_t value) { return mconf_set_typed_scalar(ctx, index, MCONF_TYPE_U8, &value, sizeof(value)); }
mconf_err_t mconf_get_u16(const mconf_t *ctx, size_t index, uint16_t *out) { return mconf_get_typed_scalar(ctx, index, MCONF_TYPE_U16, out, sizeof(*out)); }
mconf_err_t mconf_set_u16(mconf_t *ctx, size_t index, uint16_t value) { return mconf_set_typed_scalar(ctx, index, MCONF_TYPE_U16, &value, sizeof(value)); }
mconf_err_t mconf_get_u32(const mconf_t *ctx, size_t index, uint32_t *out) { return mconf_get_typed_scalar(ctx, index, MCONF_TYPE_U32, out, sizeof(*out)); }
mconf_err_t mconf_set_u32(mconf_t *ctx, size_t index, uint32_t value) { return mconf_set_typed_scalar(ctx, index, MCONF_TYPE_U32, &value, sizeof(value)); }
mconf_err_t mconf_get_i32(const mconf_t *ctx, size_t index, int32_t *out) { return mconf_get_typed_scalar(ctx, index, MCONF_TYPE_I32, out, sizeof(*out)); }
mconf_err_t mconf_set_i32(mconf_t *ctx, size_t index, int32_t value) { return mconf_set_typed_scalar(ctx, index, MCONF_TYPE_I32, &value, sizeof(value)); }
mconf_err_t mconf_get_float(const mconf_t *ctx, size_t index, float *out) { return mconf_get_typed_scalar(ctx, index, MCONF_TYPE_FLOAT32, out, sizeof(*out)); }
mconf_err_t mconf_set_float(mconf_t *ctx, size_t index, float value) { return mconf_set_typed_scalar(ctx, index, MCONF_TYPE_FLOAT32, &value, sizeof(value)); }

mconf_err_t mconf_get_blob(const mconf_t *ctx, size_t index, void *out, size_t out_size)
{
    const mconf_entry_t *entry;
    mconf_err_t err = mconf_check_index(ctx, index);
    if (err != MCONF_OK) {
        return err;
    }
    if (out == NULL) {
        return MCONF_ERR_NULL;
    }
    entry = mconf_entry_at(ctx, index);
    if (entry->type != MCONF_TYPE_BLOB) {
        return MCONF_ERR_TYPE;
    }
    return mconf_copy_field_out(entry, mconf_field_ptr_const(ctx->data, entry), out, out_size);
}

mconf_err_t mconf_set_blob(mconf_t *ctx, size_t index, const void *value, size_t value_size)
{
    const mconf_entry_t *entry;
    mconf_err_t err = mconf_check_index(ctx, index);
    if (err != MCONF_OK) {
        return err;
    }
    if (value == NULL) {
        return MCONF_ERR_NULL;
    }
    entry = mconf_entry_at(ctx, index);
    if (entry->type != MCONF_TYPE_BLOB) {
        return MCONF_ERR_TYPE;
    }
    if ((value_size == 0u) || (value_size != entry->size)) {
        return MCONF_ERR_SIZE;
    }
    memmove(mconf_field_ptr(ctx->data, entry), value, entry->size);
    return MCONF_OK;
}

mconf_err_t mconf_get_string(const mconf_t *ctx, size_t index, char *out, size_t out_size, size_t *required_size_out)
{
    const mconf_entry_t *entry;
    const char *src;
    size_t length = 0u;
    mconf_err_t err = mconf_check_index(ctx, index);
    if (err != MCONF_OK) {
        return err;
    }
    if ((out == NULL) && (out_size != 0u)) {
        return MCONF_ERR_NULL;
    }
    entry = mconf_entry_at(ctx, index);
    if (entry->type != MCONF_TYPE_STRING) {
        return MCONF_ERR_TYPE;
    }
    src = (const char *)mconf_field_ptr_const(ctx->data, entry);
    err = mconf_validate_string_bytes((const uint8_t *)src, entry->size, &length);
    if (err != MCONF_OK) {
        return err;
    }
    if (required_size_out != NULL) {
        *required_size_out = length + 1u;
    }
    if (out_size < (length + 1u)) {
        return MCONF_ERR_SIZE;
    }
    if (out != NULL) {
        memmove(out, src, length + 1u);
    }
    return MCONF_OK;
}

mconf_err_t mconf_set_string(mconf_t *ctx, size_t index, const char *value, size_t value_length, size_t *required_capacity_out)
{
    const mconf_entry_t *entry;
    char *dst;
    size_t byte_index;
    mconf_err_t err = mconf_check_index(ctx, index);
    if (err != MCONF_OK) {
        return err;
    }
    if (value == NULL) {
        return MCONF_ERR_NULL;
    }
    entry = mconf_entry_at(ctx, index);
    if (entry->type != MCONF_TYPE_STRING) {
        return MCONF_ERR_TYPE;
    }
    if (required_capacity_out != NULL) {
        *required_capacity_out = value_length + 1u;
    }
    if ((value_length + 1u) > entry->size) {
        return MCONF_ERR_SIZE;
    }
    for (byte_index = 0u; byte_index < value_length; ++byte_index) {
        if (value[byte_index] == '\0') {
            return MCONF_ERR_INVALID;
        }
    }
    dst = (char *)mconf_field_ptr(ctx->data, entry);
    memset(dst, 0, entry->size);
    memmove(dst, value, value_length);
    dst[value_length] = '\0';
    return MCONF_OK;
}

static size_t mconf_serialized_entry_size(const uint8_t *field, const mconf_entry_t *entry, mconf_err_t *err_out)
{
    size_t string_length = 0u;
    if (entry->type == MCONF_TYPE_STRING) {
        *err_out = mconf_validate_string_bytes(field, entry->size, &string_length);
        if (*err_out != MCONF_OK) {
            return 0u;
        }
        return 2u + string_length;
    }
    *err_out = MCONF_OK;
    return entry->size;
}

static mconf_err_t mconf_compute_payload_layout(const mconf_t *ctx, size_t *payload_length_out, uint32_t *crc_out)
{
    size_t index;
    size_t total = 0u;
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t encoded[4];
    mconf_err_t data_err = mconf_validate_data(ctx, ctx->data);
    if (data_err != MCONF_OK) {
        return data_err;
    }

    for (index = 0; index < ctx->schema->entry_count; ++index) {
        const mconf_entry_t *entry = mconf_entry_at(ctx, index);
        const uint8_t *field = (const uint8_t *)mconf_field_ptr_const(ctx->data, entry);
        mconf_err_t entry_err = MCONF_OK;
        size_t entry_size = mconf_serialized_entry_size(field, entry, &entry_err);
        size_t byte_index;
        if (entry_err != MCONF_OK) {
            return entry_err;
        }
        if (total > (SIZE_MAX - entry_size)) {
            return MCONF_ERR_SIZE;
        }
        total += entry_size;

        if (entry->type == MCONF_TYPE_STRING) {
            size_t length = 0u;
            mconf_validate_string_bytes(field, entry->size, &length);
            mconf_crc32_update_byte(&crc, (uint8_t)(length & 0xFFu));
            mconf_crc32_update_byte(&crc, (uint8_t)((length >> 8) & 0xFFu));
            for (byte_index = 0u; byte_index < length; ++byte_index) {
                mconf_crc32_update_byte(&crc, field[byte_index]);
            }
        } else if (entry->type == MCONF_TYPE_BLOB) {
            for (byte_index = 0u; byte_index < entry->size; ++byte_index) {
                mconf_crc32_update_byte(&crc, field[byte_index]);
            }
        } else {
            size_t encoded_size = 0u;
            entry_err = mconf_encode_field_bytes(entry, field, encoded, &encoded_size);
            if (entry_err != MCONF_OK) {
                return entry_err;
            }
            for (byte_index = 0u; byte_index < encoded_size; ++byte_index) {
                mconf_crc32_update_byte(&crc, encoded[byte_index]);
            }
        }
    }

    if (payload_length_out != NULL) {
        *payload_length_out = total;
    }
    if (crc_out != NULL) {
        *crc_out = mconf_crc32_finalize(crc);
    }
    return MCONF_OK;
}

static mconf_err_t mconf_encode_header(uint8_t *header, const mconf_t *ctx, uint32_t generation, uint32_t payload_length, uint32_t payload_crc32, uint8_t state)
{
    memset(header, 0, MCONF_STORAGE_HEADER_SIZE);
    header[0] = MCONF_STORAGE_MAGIC_0;
    header[1] = MCONF_STORAGE_MAGIC_1;
    header[2] = MCONF_STORAGE_MAGIC_2;
    header[3] = MCONF_STORAGE_MAGIC_3;
    mconf_write_u16_le(&header[4], MCONF_STORAGE_FORMAT_VERSION);
    mconf_write_u16_le(&header[6], (uint16_t)MCONF_STORAGE_HEADER_SIZE);
    mconf_write_u32_le(&header[8], ctx->schema->schema_version);
    mconf_write_u32_le(&header[12], ctx->schema_fingerprint);
    if (ctx->schema->entry_count > MCONF_U16_MAX_VALUE) {
        return MCONF_ERR_SIZE;
    }
    mconf_write_u16_le(&header[16], (uint16_t)ctx->schema->entry_count);
    header[MCONF_STORAGE_STATE_OFFSET] = state;
    header[MCONF_STORAGE_STATE_OFFSET + 1u] = 0u;
    mconf_write_u32_le(&header[20], payload_length);
    mconf_write_u32_le(&header[24], generation);
    mconf_write_u32_le(&header[28], payload_crc32);
    return MCONF_OK;
}

static mconf_err_t mconf_io_read_exact(const mconf_io_t *io, size_t offset, void *buffer, size_t size)
{
    if (io->read == NULL) {
        return MCONF_ERR_NULL;
    }
    if (io->read(io->callback_ctx, offset, buffer, size) != 0) {
        return MCONF_ERR_IO;
    }
    return MCONF_OK;
}

static mconf_err_t mconf_io_write_exact(const mconf_io_t *io, size_t offset, const void *buffer, size_t size)
{
    if (io->write == NULL) {
        return MCONF_ERR_NULL;
    }
    if (io->write(io->callback_ctx, offset, buffer, size) != 0) {
        return MCONF_ERR_IO;
    }
    return MCONF_OK;
}

static mconf_err_t mconf_serialize_payload_to_storage(const mconf_t *ctx, const mconf_io_t *io, size_t base_offset)
{
    size_t index;
    size_t offset = base_offset;
    uint8_t encoded[4];
    for (index = 0; index < ctx->schema->entry_count; ++index) {
        const mconf_entry_t *entry = mconf_entry_at(ctx, index);
        const uint8_t *field = (const uint8_t *)mconf_field_ptr_const(ctx->data, entry);
        if (entry->type == MCONF_TYPE_STRING) {
            size_t length = 0u;
            uint8_t length_le[2];
            mconf_err_t err = mconf_validate_string_bytes(field, entry->size, &length);
            if (err != MCONF_OK) {
                return err;
            }
            mconf_write_u16_le(length_le, (uint16_t)length);
            err = mconf_io_write_exact(io, offset, length_le, sizeof(length_le));
            if (err != MCONF_OK) {
                return err;
            }
            offset += sizeof(length_le);
            if (length != 0u) {
                err = mconf_io_write_exact(io, offset, field, length);
                if (err != MCONF_OK) {
                    return err;
                }
                offset += length;
            }
        } else if (entry->type == MCONF_TYPE_BLOB) {
            mconf_err_t err = mconf_io_write_exact(io, offset, field, entry->size);
            if (err != MCONF_OK) {
                return err;
            }
            offset += entry->size;
        } else {
            size_t encoded_size = 0u;
            mconf_err_t err = mconf_encode_field_bytes(entry, field, encoded, &encoded_size);
            if (err != MCONF_OK) {
                return err;
            }
            err = mconf_io_write_exact(io, offset, encoded, encoded_size);
            if (err != MCONF_OK) {
                return err;
            }
            offset += encoded_size;
        }
    }
    return MCONF_OK;
}

static mconf_err_t mconf_decode_header(const uint8_t *header, mconf_record_info_t *info)
{
    if ((header[0] != MCONF_STORAGE_MAGIC_0) || (header[1] != MCONF_STORAGE_MAGIC_1) ||
        (header[2] != MCONF_STORAGE_MAGIC_2) || (header[3] != MCONF_STORAGE_MAGIC_3)) {
        return MCONF_ERR_MAGIC;
    }
    if (mconf_read_u16_le(&header[6]) != (uint16_t)MCONF_STORAGE_HEADER_SIZE) {
        return MCONF_ERR_INVALID;
    }

    info->format_version = mconf_read_u16_le(&header[4]);
    info->schema_version = mconf_read_u32_le(&header[8]);
    info->schema_fingerprint = mconf_read_u32_le(&header[12]);
    info->entry_count = mconf_read_u16_le(&header[16]);
    info->payload_length = mconf_read_u32_le(&header[20]);
    info->generation = mconf_read_u32_le(&header[24]);
    info->payload_crc32 = mconf_read_u32_le(&header[28]);
    info->state = header[MCONF_STORAGE_STATE_OFFSET];
    return MCONF_OK;
}

static mconf_err_t mconf_probe_slot_header(const mconf_t *ctx, const mconf_io_t *io, size_t slot_index, bool require_committed, mconf_record_info_t *info_out)
{
    uint8_t header[MCONF_STORAGE_HEADER_SIZE];
    mconf_record_info_t info;
    mconf_err_t err;

    if (((slot_index + 1u) * io->slot_size) > io->storage_size) {
        return MCONF_ERR_SIZE;
    }

    err = mconf_io_read_exact(io, slot_index * io->slot_size, header, sizeof(header));
    if (err != MCONF_OK) {
        return err;
    }
    err = mconf_decode_header(header, &info);
    if (err != MCONF_OK) {
        return err;
    }
    if (((require_committed) && (info.state != MCONF_STORAGE_STATE_COMMITTED)) ||
        ((!require_committed) && (info.state != MCONF_STORAGE_STATE_COMMITTED) && (info.state != MCONF_STORAGE_STATE_UNCOMMITTED))) {
        return MCONF_ERR_INVALID;
    }
    if (info.format_version != MCONF_STORAGE_FORMAT_VERSION) {
        return MCONF_ERR_INVALID;
    }
    if ((info.schema_version != ctx->schema->schema_version) || (info.schema_fingerprint != ctx->schema_fingerprint)) {
        return MCONF_ERR_VERSION;
    }
    if (info.entry_count != ctx->schema->entry_count) {
        return MCONF_ERR_INVALID;
    }
    if ((MCONF_STORAGE_HEADER_SIZE + info.payload_length) > io->slot_size) {
        return MCONF_ERR_SIZE;
    }

    if (info_out != NULL) {
        *info_out = info;
    }
    return MCONF_OK;
}

static mconf_err_t mconf_load_slot_into_buffer(const mconf_t *ctx, const mconf_io_t *io, size_t slot_index, void *buffer, mconf_record_info_t *info_out, bool require_committed)
{
    size_t payload_offset;
    size_t running_offset;
    size_t index;
    uint32_t crc = 0xFFFFFFFFu;
    mconf_record_info_t info;
    uint8_t *scratch = (uint8_t *)buffer;
    uint8_t encoded[4];
    mconf_err_t err;

    err = mconf_probe_slot_header(ctx, io, slot_index, require_committed, &info);
    if (err != MCONF_OK) {
        return err;
    }
    payload_offset = slot_index * io->slot_size + MCONF_STORAGE_HEADER_SIZE;
    running_offset = payload_offset;
    memset(scratch, 0, ctx->data_size);

    for (index = 0; index < ctx->schema->entry_count; ++index) {
        const mconf_entry_t *entry = mconf_entry_at(ctx, index);
        uint8_t *dst = (uint8_t *)mconf_field_ptr(scratch, entry);
        if (entry->type == MCONF_TYPE_STRING) {
            uint8_t length_le[2];
            uint16_t length;
            err = mconf_io_read_exact(io, running_offset, length_le, sizeof(length_le));
            if (err != MCONF_OK) {
                return err;
            }
            running_offset += sizeof(length_le);
            mconf_crc32_update_byte(&crc, length_le[0]);
            mconf_crc32_update_byte(&crc, length_le[1]);
            length = mconf_read_u16_le(length_le);
            if ((length + 1u) > entry->size) {
                return MCONF_ERR_INVALID;
            }
            memset(dst, 0, entry->size);
            if (length != 0u) {
                err = mconf_io_read_exact(io, running_offset, dst, length);
                if (err != MCONF_OK) {
                    return err;
                }
                running_offset += length;
                {
                    size_t byte_index;
                    for (byte_index = 0u; byte_index < length; ++byte_index) {
                        mconf_crc32_update_byte(&crc, dst[byte_index]);
                    }
                }
            }
            dst[length] = 0u;
        } else if (entry->type == MCONF_TYPE_BLOB) {
            size_t byte_index;
            err = mconf_io_read_exact(io, running_offset, dst, entry->size);
            if (err != MCONF_OK) {
                return err;
            }
            running_offset += entry->size;
            for (byte_index = 0u; byte_index < entry->size; ++byte_index) {
                mconf_crc32_update_byte(&crc, dst[byte_index]);
            }
        } else {
            size_t encoded_size = mconf_expected_type_size(entry->type);
            size_t byte_index;
            err = mconf_io_read_exact(io, running_offset, encoded, encoded_size);
            if (err != MCONF_OK) {
                return err;
            }
            running_offset += encoded_size;
            for (byte_index = 0u; byte_index < encoded_size; ++byte_index) {
                mconf_crc32_update_byte(&crc, encoded[byte_index]);
            }
            err = mconf_decode_field_bytes(entry, encoded, encoded_size, dst);
            if (err != MCONF_OK) {
                return err;
            }
        }
    }

    if ((running_offset - payload_offset) != info.payload_length) {
        return MCONF_ERR_SIZE;
    }
    if (mconf_crc32_finalize(crc) != info.payload_crc32) {
        return MCONF_ERR_CRC;
    }
    if (mconf_validate_data(ctx, scratch) != MCONF_OK) {
        return MCONF_ERR_INVALID;
    }
    if (info_out != NULL) {
        *info_out = info;
    }
    return MCONF_OK;
}

static bool mconf_generation_is_newer(uint32_t lhs, uint32_t rhs)
{
    return ((int32_t)(lhs - rhs)) > 0;
}

mconf_err_t mconf_load(mconf_t *ctx, const mconf_io_t *io)
{
    mconf_record_info_t slot0_info;
    mconf_record_info_t slot1_info;
    mconf_err_t slot0_err;
    mconf_err_t slot1_err;
    mconf_err_t ctx_err = mconf_require_context(ctx);
    if (ctx_err != MCONF_OK) {
        return ctx_err;
    }
    if ((io == NULL) || (io->read == NULL)) {
        return MCONF_ERR_NULL;
    }
    if ((io->slot_size < MCONF_STORAGE_HEADER_SIZE) || (io->storage_size < (io->slot_size * 2u))) {
        return MCONF_ERR_SIZE;
    }

    slot0_err = mconf_probe_slot_header(ctx, io, 0u, true, &slot0_info);
    slot1_err = mconf_probe_slot_header(ctx, io, 1u, true, &slot1_info);

    if ((slot0_err == MCONF_OK) && (slot1_err == MCONF_OK)) {
        if (mconf_generation_is_newer(slot1_info.generation, slot0_info.generation)) {
            if (mconf_load_slot_into_buffer(ctx, io, 1u, ctx->data, NULL, true) == MCONF_OK) {
                return MCONF_OK;
            }
            if (mconf_load_slot_into_buffer(ctx, io, 0u, ctx->data, NULL, true) == MCONF_OK) {
                return MCONF_OK;
            }
        } else {
            if (mconf_load_slot_into_buffer(ctx, io, 0u, ctx->data, NULL, true) == MCONF_OK) {
                return MCONF_OK;
            }
            if (mconf_load_slot_into_buffer(ctx, io, 1u, ctx->data, NULL, true) == MCONF_OK) {
                return MCONF_OK;
            }
        }
    } else if (slot0_err == MCONF_OK) {
        if (mconf_load_slot_into_buffer(ctx, io, 0u, ctx->data, NULL, true) == MCONF_OK) {
            return MCONF_OK;
        }
    } else if (slot1_err == MCONF_OK) {
        if (mconf_load_slot_into_buffer(ctx, io, 1u, ctx->data, NULL, true) == MCONF_OK) {
            return MCONF_OK;
        }
    }

    mconf_load_defaults(ctx);
    if (slot0_err == MCONF_ERR_CRC || slot1_err == MCONF_ERR_CRC) {
        return MCONF_ERR_CRC;
    }
    if (slot0_err == MCONF_ERR_VERSION || slot1_err == MCONF_ERR_VERSION) {
        return MCONF_ERR_VERSION;
    }
    if (slot0_err == MCONF_ERR_MAGIC || slot1_err == MCONF_ERR_MAGIC) {
        return MCONF_ERR_MAGIC;
    }
    return MCONF_ERR_INVALID;
}

mconf_err_t mconf_save(mconf_t *ctx, const mconf_io_t *io)
{
    uint8_t header[MCONF_STORAGE_HEADER_SIZE];
    uint8_t verify_header[MCONF_STORAGE_HEADER_SIZE];
    uint8_t committed_state = MCONF_STORAGE_STATE_COMMITTED;
    mconf_record_info_t slot0_info;
    mconf_record_info_t slot1_info;
    mconf_err_t slot0_err;
    mconf_err_t slot1_err;
    size_t payload_length = 0u;
    uint32_t payload_crc = 0u;
    uint32_t next_generation = 1u;
    size_t target_slot = 0u;
    size_t target_offset;
    mconf_err_t err = mconf_require_context(ctx);
    if (err != MCONF_OK) {
        return err;
    }
    if ((io == NULL) || (io->read == NULL) || (io->write == NULL) || (io->erase == NULL)) {
        return MCONF_ERR_NULL;
    }
    if ((io->slot_size < MCONF_STORAGE_HEADER_SIZE) || (io->storage_size < (io->slot_size * 2u))) {
        return MCONF_ERR_SIZE;
    }

    err = mconf_compute_payload_layout(ctx, &payload_length, &payload_crc);
    if (err != MCONF_OK) {
        return err;
    }
    if ((MCONF_STORAGE_HEADER_SIZE + payload_length) > io->slot_size) {
        return MCONF_ERR_SIZE;
    }

    slot0_err = mconf_probe_slot_header(ctx, io, 0u, true, &slot0_info);
    slot1_err = mconf_probe_slot_header(ctx, io, 1u, true, &slot1_info);
    if ((slot0_err == MCONF_OK) && (slot1_err == MCONF_OK)) {
        if (mconf_generation_is_newer(slot1_info.generation, slot0_info.generation)) {
            target_slot = 0u;
            next_generation = slot1_info.generation + 1u;
        } else {
            target_slot = 1u;
            next_generation = slot0_info.generation + 1u;
        }
    } else if (slot0_err == MCONF_OK) {
        target_slot = 1u;
        next_generation = slot0_info.generation + 1u;
    } else if (slot1_err == MCONF_OK) {
        target_slot = 0u;
        next_generation = slot1_info.generation + 1u;
    }
    if (next_generation == 0u) {
        next_generation = 1u;
    }

    err = mconf_encode_header(header, ctx, next_generation, (uint32_t)payload_length, payload_crc, MCONF_STORAGE_STATE_UNCOMMITTED);
    if (err != MCONF_OK) {
        return err;
    }
    target_offset = target_slot * io->slot_size;
    if (io->erase(io->callback_ctx, target_offset, io->slot_size) != 0) {
        return MCONF_ERR_IO;
    }
    err = mconf_io_write_exact(io, target_offset, header, sizeof(header));
    if (err != MCONF_OK) {
        return err;
    }
    err = mconf_serialize_payload_to_storage(ctx, io, target_offset + MCONF_STORAGE_HEADER_SIZE);
    if (err != MCONF_OK) {
        return err;
    }
    err = mconf_io_read_exact(io, target_offset, verify_header, sizeof(verify_header));
    if (err != MCONF_OK) {
        return err;
    }
    if (memcmp(verify_header, header, sizeof(header)) != 0) {
        return MCONF_ERR_IO;
    }
    err = mconf_load_slot_into_buffer(ctx, io, target_slot, ctx->data, NULL, false);
    if (err != MCONF_OK) {
        return err;
    }
    err = mconf_io_write_exact(io, target_offset + MCONF_STORAGE_STATE_OFFSET, &committed_state, sizeof(committed_state));
    if (err != MCONF_OK) {
        return err;
    }
    return MCONF_OK;
}
