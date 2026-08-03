#ifndef PLS_IMAGE_H
#define PLS_IMAGE_H

#include "flash_sim.h"
#include "sha256.h"

#include <stddef.h>
#include <stdint.h>

#define IMAGE_HEADER_SIZE 64U
#define IMAGE_HASH_SIZE SHA256_DIGEST_SIZE
#define IMAGE_COMMIT_SIZE 4U
#define IMAGE_MAGIC_OFFSET 0U
#define IMAGE_FORMAT_OFFSET 4U
#define IMAGE_FIRMWARE_VERSION_OFFSET 8U
#define IMAGE_PAYLOAD_SIZE_OFFSET 16U
#define IMAGE_PAYLOAD_HASH_OFFSET 20U
#define IMAGE_FLAGS_OFFSET 52U
#define IMAGE_HEADER_CRC_OFFSET 56U
#define IMAGE_COMMIT_OFFSET 60U
#define IMAGE_FORMAT_VERSION 1U
#define IMAGE_MAGIC 0x31534C50U
#define IMAGE_COMMIT_MARKER 0x54494D43U

typedef struct {
    uint64_t base_address;
    uint64_t size;
} image_slot_t;

typedef struct {
    uint32_t format_version;
    uint64_t firmware_version;
    uint32_t payload_size;
    uint8_t payload_hash[IMAGE_HASH_SIZE];
} image_info_t;

typedef enum {
    IMAGE_OK = 0,
    IMAGE_ERR_INVALID_ARGUMENT,
    IMAGE_ERR_SLOT_BOUNDS,
    IMAGE_ERR_FLASH,
    IMAGE_ERR_POWER_LOSS,
    IMAGE_ERR_UNCOMMITTED,
    IMAGE_ERR_BAD_MAGIC,
    IMAGE_ERR_UNSUPPORTED_FORMAT,
    IMAGE_ERR_UNSUPPORTED_FLAGS,
    IMAGE_ERR_BAD_HEADER_CRC,
    IMAGE_ERR_INVALID_SIZE,
    IMAGE_ERR_HASH_MISMATCH
} image_status_t;

image_status_t image_build_uncommitted_header(
    uint8_t header[IMAGE_HEADER_SIZE],
    uint64_t firmware_version,
    const void *payload,
    size_t payload_size);

image_status_t image_publish_commit_marker(
    flash_t *flash,
    const image_slot_t *slot);

image_status_t image_validate(
    flash_t *flash,
    const image_slot_t *slot,
    image_info_t *out_info);

const char *image_status_string(image_status_t status);

#endif
