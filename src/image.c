#include "image.h"

#include "crc32.h"
#include "flash_sim.h"
#include "sha256.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define IMAGE_SUPPORTED_FLAGS 0U
#define IMAGE_HASH_READ_CHUNK 256U

static uint32_t load_le32(const uint8_t bytes[static 4])
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static uint64_t load_le64(const uint8_t bytes[static 8])
{
    return (uint64_t)bytes[0] |
           ((uint64_t)bytes[1] << 8U) |
           ((uint64_t)bytes[2] << 16U) |
           ((uint64_t)bytes[3] << 24U) |
           ((uint64_t)bytes[4] << 32U) |
           ((uint64_t)bytes[5] << 40U) |
           ((uint64_t)bytes[6] << 48U) |
           ((uint64_t)bytes[7] << 56U);
}

static void store_le32(uint8_t bytes[static 4], uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static void store_le64(uint8_t bytes[static 8], uint64_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
    bytes[4] = (uint8_t)(value >> 32U);
    bytes[5] = (uint8_t)(value >> 40U);
    bytes[6] = (uint8_t)(value >> 48U);
    bytes[7] = (uint8_t)(value >> 56U);
}

static image_status_t image_status_from_flash(flash_status_t status)
{
    if (status == FLASH_ERR_POWER_LOSS) {
        return IMAGE_ERR_POWER_LOSS;
    }

    return status == FLASH_OK ? IMAGE_OK : IMAGE_ERR_FLASH;
}

static bool slot_is_in_flash(const flash_t *flash, const image_slot_t *slot)
{
    const flash_geometry_t *geometry;

    if (flash == NULL || slot == NULL || slot->size < IMAGE_HEADER_SIZE) {
        return false;
    }

    geometry = flash_geometry(flash);
    if (geometry == NULL || slot->base_address > geometry->total_size) {
        return false;
    }

    return slot->size <= geometry->total_size - slot->base_address;
}

static bool hashes_equal(
    const uint8_t first[IMAGE_HASH_SIZE],
    const uint8_t second[IMAGE_HASH_SIZE])
{
    uint8_t difference = 0U;
    size_t index;

    for (index = 0U; index < IMAGE_HASH_SIZE; ++index) {
        difference |= first[index] ^ second[index];
    }

    return difference == 0U;
}

image_status_t image_build_uncommitted_header(
    uint8_t header[IMAGE_HEADER_SIZE],
    uint64_t firmware_version,
    const void *payload,
    size_t payload_size)
{
    uint32_t header_crc;

    if (header == NULL || payload == NULL || payload_size == 0U ||
        payload_size > UINT32_MAX) {
        return IMAGE_ERR_INVALID_ARGUMENT;
    }

    (void)memset(header, 0xFF, IMAGE_HEADER_SIZE);
    store_le32(header + IMAGE_MAGIC_OFFSET, IMAGE_MAGIC);
    store_le32(header + IMAGE_FORMAT_OFFSET, IMAGE_FORMAT_VERSION);
    store_le64(header + IMAGE_FIRMWARE_VERSION_OFFSET, firmware_version);
    store_le32(header + IMAGE_PAYLOAD_SIZE_OFFSET, (uint32_t)payload_size);
    sha256_compute(payload, payload_size, header + IMAGE_PAYLOAD_HASH_OFFSET);
    store_le32(header + IMAGE_FLAGS_OFFSET, IMAGE_SUPPORTED_FLAGS);

    header_crc = crc32_compute(header, IMAGE_HEADER_CRC_OFFSET);
    store_le32(header + IMAGE_HEADER_CRC_OFFSET, header_crc);
    return IMAGE_OK;
}

image_status_t image_publish_commit_marker(
    flash_t *flash,
    const image_slot_t *slot)
{
    uint8_t commit_bytes[IMAGE_COMMIT_SIZE];
    flash_status_t status;

    if (!slot_is_in_flash(flash, slot)) {
        return IMAGE_ERR_SLOT_BOUNDS;
    }

    store_le32(commit_bytes, IMAGE_COMMIT_MARKER);
    status = flash_program(
        flash,
        slot->base_address + IMAGE_COMMIT_OFFSET,
        commit_bytes,
        sizeof(commit_bytes));
    return image_status_from_flash(status);
}

image_status_t image_validate(
    flash_t *flash,
    const image_slot_t *slot,
    image_info_t *out_info)
{
    uint8_t header[IMAGE_HEADER_SIZE];
    uint8_t computed_hash[IMAGE_HASH_SIZE];
    uint8_t read_buffer[IMAGE_HASH_READ_CHUNK];
    sha256_context_t hash_context;
    uint64_t payload_address;
    uint32_t payload_size;
    uint32_t expected_crc;
    uint32_t computed_crc;
    uint32_t flags;
    size_t remaining;
    flash_status_t flash_status;

    if (flash == NULL || slot == NULL || out_info == NULL) {
        return IMAGE_ERR_INVALID_ARGUMENT;
    }

    (void)memset(out_info, 0, sizeof(*out_info));

    if (!slot_is_in_flash(flash, slot)) {
        return IMAGE_ERR_SLOT_BOUNDS;
    }

    flash_status = flash_read(
        flash,
        slot->base_address,
        header,
        sizeof(header));
    if (flash_status != FLASH_OK) {
        return image_status_from_flash(flash_status);
    }

    if (load_le32(header + IMAGE_COMMIT_OFFSET) != IMAGE_COMMIT_MARKER) {
        return IMAGE_ERR_UNCOMMITTED;
    }

    if (load_le32(header + IMAGE_MAGIC_OFFSET) != IMAGE_MAGIC) {
        return IMAGE_ERR_BAD_MAGIC;
    }

    if (load_le32(header + IMAGE_FORMAT_OFFSET) != IMAGE_FORMAT_VERSION) {
        return IMAGE_ERR_UNSUPPORTED_FORMAT;
    }

    flags = load_le32(header + IMAGE_FLAGS_OFFSET);
    if (flags != IMAGE_SUPPORTED_FLAGS) {
        return IMAGE_ERR_UNSUPPORTED_FLAGS;
    }

    expected_crc = load_le32(header + IMAGE_HEADER_CRC_OFFSET);
    computed_crc = crc32_compute(header, IMAGE_HEADER_CRC_OFFSET);
    if (computed_crc != expected_crc) {
        return IMAGE_ERR_BAD_HEADER_CRC;
    }

    payload_size = load_le32(header + IMAGE_PAYLOAD_SIZE_OFFSET);
    if (payload_size == 0U ||
        (uint64_t)payload_size > slot->size - IMAGE_HEADER_SIZE) {
        return IMAGE_ERR_INVALID_SIZE;
    }

    payload_address = slot->base_address + IMAGE_HEADER_SIZE;
    remaining = payload_size;
    sha256_init(&hash_context);

    while (remaining > 0U) {
        const size_t chunk = remaining < sizeof(read_buffer)
                                 ? remaining
                                 : sizeof(read_buffer);

        flash_status = flash_read(flash, payload_address, read_buffer, chunk);
        if (flash_status != FLASH_OK) {
            return image_status_from_flash(flash_status);
        }

        sha256_update(&hash_context, read_buffer, chunk);
        payload_address += (uint64_t)chunk;
        remaining -= chunk;
    }

    sha256_final(&hash_context, computed_hash);
    if (!hashes_equal(computed_hash, header + IMAGE_PAYLOAD_HASH_OFFSET)) {
        return IMAGE_ERR_HASH_MISMATCH;
    }

    out_info->format_version = IMAGE_FORMAT_VERSION;
    out_info->firmware_version = load_le64(
        header + IMAGE_FIRMWARE_VERSION_OFFSET);
    out_info->payload_size = payload_size;
    (void)memcpy(
        out_info->payload_hash,
        header + IMAGE_PAYLOAD_HASH_OFFSET,
        IMAGE_HASH_SIZE);
    return IMAGE_OK;
}

const char *image_status_string(image_status_t status)
{
    switch (status) {
    case IMAGE_OK:
        return "ok";
    case IMAGE_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case IMAGE_ERR_SLOT_BOUNDS:
        return "slot is outside flash bounds";
    case IMAGE_ERR_FLASH:
        return "flash access failed";
    case IMAGE_ERR_POWER_LOSS:
        return "simulated power loss";
    case IMAGE_ERR_UNCOMMITTED:
        return "image is not committed";
    case IMAGE_ERR_BAD_MAGIC:
        return "image magic is invalid";
    case IMAGE_ERR_UNSUPPORTED_FORMAT:
        return "image format is unsupported";
    case IMAGE_ERR_UNSUPPORTED_FLAGS:
        return "image flags are unsupported";
    case IMAGE_ERR_BAD_HEADER_CRC:
        return "image header CRC is invalid";
    case IMAGE_ERR_INVALID_SIZE:
        return "image payload size is invalid";
    case IMAGE_ERR_HASH_MISMATCH:
        return "image payload hash does not match";
    default:
        return "unknown image status";
    }
}
