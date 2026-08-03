#include "boot_selector.h"
#include "crc32.h"
#include "flash_sim.h"
#include "image.h"
#include "sha256.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                     \
                    __FILE__, __LINE__, #condition);                            \
            ok = false;                                                         \
            goto cleanup;                                                       \
        }                                                                       \
    } while (false)

static const flash_geometry_t TEST_GEOMETRY = {
    .total_size = 4096U,
    .sector_size = 256U,
    .program_unit = 4U,
};

static const firmware_slots_t TEST_SLOTS = {
    .slots = {
        {.base_address = 1024U, .size = 1536U},
        {.base_address = 2560U, .size = 1536U},
    },
};

static const uint8_t TEST_PAYLOAD[] = {
    0xFFU, 0x10U, 0x22U, 0x34U, 0x46U, 0x58U, 0x6AU,
    0x7CU, 0x8EU, 0x90U, 0xA2U, 0xB4U, 0xC6U,
};

static bool make_flash_path(char path[static 32])
{
    int fd;

    (void)strcpy(path, "/tmp/pls-image-test-XXXXXX");
    fd = mkstemp(path);
    if (fd < 0) {
        return false;
    }

    return close(fd) == 0;
}

static void store_le32(uint8_t bytes[static 4], uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static void update_header_crc(uint8_t header[IMAGE_HEADER_SIZE])
{
    const uint32_t crc = crc32_compute(header, IMAGE_HEADER_CRC_OFFSET);
    store_le32(header + IMAGE_HEADER_CRC_OFFSET, crc);
}

static bool create_open_flash(const char *path, flash_t **out_flash)
{
    return flash_format(path, &TEST_GEOMETRY) == FLASH_OK &&
           flash_open(out_flash, path, &TEST_GEOMETRY) == FLASH_OK;
}

static bool program_padded(
    flash_t *flash,
    uint64_t address,
    const uint8_t *data,
    size_t length)
{
    const uint64_t unit = flash_geometry(flash)->program_unit;
    const size_t padded_length = (size_t)(((uint64_t)length + unit - 1U) / unit * unit);
    uint8_t *padded;
    flash_status_t status;

    padded = malloc(padded_length);
    if (padded == NULL) {
        return false;
    }

    (void)memset(padded, 0xFF, padded_length);
    (void)memcpy(padded, data, length);
    status = flash_program(flash, address, padded, padded_length);
    free(padded);
    return status == FLASH_OK;
}

static bool program_uncommitted_image(
    flash_t *flash,
    const image_slot_t *slot,
    const uint8_t header[IMAGE_HEADER_SIZE],
    const uint8_t *payload,
    size_t payload_size)
{
    if (payload_size > 0U &&
        !program_padded(
            flash,
            slot->base_address + IMAGE_HEADER_SIZE,
            payload,
            payload_size)) {
        return false;
    }

    return flash_program(
               flash,
               slot->base_address,
               header,
               IMAGE_HEADER_SIZE) == FLASH_OK;
}

static bool provision_committed_image(
    flash_t *flash,
    const image_slot_t *slot,
    uint64_t firmware_version,
    const uint8_t *payload,
    size_t payload_size)
{
    uint8_t header[IMAGE_HEADER_SIZE];

    return image_build_uncommitted_header(
               header,
               firmware_version,
               payload,
               payload_size) == IMAGE_OK &&
           program_uncommitted_image(
               flash,
               slot,
               header,
               payload,
               payload_size) &&
           image_publish_commit_marker(flash, slot) == IMAGE_OK;
}

static bool test_crc32_known_vector(void)
{
    bool ok = true;

    CHECK(crc32_compute("123456789", 9U) == 0xCBF43926U);

cleanup:
    return ok;
}

static bool test_sha256_known_vectors(void)
{
    static const uint8_t EMPTY_DIGEST[SHA256_DIGEST_SIZE] = {
        0xE3U, 0xB0U, 0xC4U, 0x42U, 0x98U, 0xFCU, 0x1CU, 0x14U,
        0x9AU, 0xFBU, 0xF4U, 0xC8U, 0x99U, 0x6FU, 0xB9U, 0x24U,
        0x27U, 0xAEU, 0x41U, 0xE4U, 0x64U, 0x9BU, 0x93U, 0x4CU,
        0xA4U, 0x95U, 0x99U, 0x1BU, 0x78U, 0x52U, 0xB8U, 0x55U,
    };
    static const uint8_t ABC_DIGEST[SHA256_DIGEST_SIZE] = {
        0xBAU, 0x78U, 0x16U, 0xBFU, 0x8FU, 0x01U, 0xCFU, 0xEAU,
        0x41U, 0x41U, 0x40U, 0xDEU, 0x5DU, 0xAEU, 0x22U, 0x23U,
        0xB0U, 0x03U, 0x61U, 0xA3U, 0x96U, 0x17U, 0x7AU, 0x9CU,
        0xB4U, 0x10U, 0xFFU, 0x61U, 0xF2U, 0x00U, 0x15U, 0xADU,
    };
    uint8_t digest[SHA256_DIGEST_SIZE];
    bool ok = true;

    sha256_compute("", 0U, digest);
    CHECK(memcmp(digest, EMPTY_DIGEST, sizeof(digest)) == 0);
    sha256_compute("abc", 3U, digest);
    CHECK(memcmp(digest, ABC_DIGEST, sizeof(digest)) == 0);

cleanup:
    return ok;
}

static bool test_valid_confirmed_a_is_selected(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    boot_decision_t decision;
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(create_open_flash(path, &flash));
    CHECK(provision_committed_image(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        42U,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)));
    CHECK(boot_select_confirmed(
        flash,
        &TEST_SLOTS,
        BOOT_SLOT_A,
        &decision) == BOOT_OK);
    CHECK(decision.selected_slot == BOOT_SLOT_A);
    CHECK(decision.validation_status == IMAGE_OK);
    CHECK(decision.image.firmware_version == 42U);
    CHECK(decision.image.payload_size == sizeof(TEST_PAYLOAD));

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_uncommitted_image_is_rejected(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    uint8_t header[IMAGE_HEADER_SIZE];
    boot_decision_t decision;
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(create_open_flash(path, &flash));
    CHECK(image_build_uncommitted_header(
        header,
        1U,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)) == IMAGE_OK);
    CHECK(program_uncommitted_image(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        header,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)));
    CHECK(boot_select_confirmed(
        flash,
        &TEST_SLOTS,
        BOOT_SLOT_A,
        &decision) == BOOT_NO_BOOTABLE_IMAGE);
    CHECK(decision.selected_slot == BOOT_SLOT_NONE);
    CHECK(decision.validation_status == IMAGE_ERR_UNCOMMITTED);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_partial_commit_marker_is_rejected_after_reopen(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    uint8_t header[IMAGE_HEADER_SIZE];
    boot_decision_t decision;
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(create_open_flash(path, &flash));
    CHECK(image_build_uncommitted_header(
        header,
        2U,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)) == IMAGE_OK);
    CHECK(program_uncommitted_image(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        header,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)));
    CHECK(flash_inject_power_failure_after(flash, 2U) == FLASH_OK);
    CHECK(image_publish_commit_marker(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A]) == IMAGE_ERR_POWER_LOSS);

    flash_close(flash);
    flash = NULL;
    CHECK(flash_open(&flash, path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(boot_select_confirmed(
        flash,
        &TEST_SLOTS,
        BOOT_SLOT_A,
        &decision) == BOOT_NO_BOOTABLE_IMAGE);
    CHECK(decision.validation_status == IMAGE_ERR_UNCOMMITTED);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_complete_commit_is_accepted_when_caller_loses_power(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    uint8_t header[IMAGE_HEADER_SIZE];
    boot_decision_t decision;
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(create_open_flash(path, &flash));
    CHECK(image_build_uncommitted_header(
        header,
        17U,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)) == IMAGE_OK);
    CHECK(program_uncommitted_image(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        header,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)));
    CHECK(flash_inject_power_failure_after(flash, IMAGE_COMMIT_SIZE) == FLASH_OK);
    CHECK(image_publish_commit_marker(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A]) == IMAGE_ERR_POWER_LOSS);

    flash_close(flash);
    flash = NULL;
    CHECK(flash_open(&flash, path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(boot_select_confirmed(
        flash,
        &TEST_SLOTS,
        BOOT_SLOT_A,
        &decision) == BOOT_OK);
    CHECK(decision.selected_slot == BOOT_SLOT_A);
    CHECK(decision.image.firmware_version == 17U);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_bad_magic_is_rejected(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    uint8_t header[IMAGE_HEADER_SIZE];
    image_info_t info;
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(create_open_flash(path, &flash));
    CHECK(image_build_uncommitted_header(
        header,
        3U,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)) == IMAGE_OK);
    header[0] = 0x00U;
    update_header_crc(header);
    CHECK(program_uncommitted_image(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        header,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)));
    CHECK(image_publish_commit_marker(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A]) == IMAGE_OK);
    CHECK(image_validate(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        &info) == IMAGE_ERR_BAD_MAGIC);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_bad_header_crc_is_rejected(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    uint8_t header[IMAGE_HEADER_SIZE];
    image_info_t info;
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(create_open_flash(path, &flash));
    CHECK(image_build_uncommitted_header(
        header,
        4U,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)) == IMAGE_OK);
    header[8] ^= 0x01U;
    CHECK(program_uncommitted_image(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        header,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)));
    CHECK(image_publish_commit_marker(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A]) == IMAGE_OK);
    CHECK(image_validate(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        &info) == IMAGE_ERR_BAD_HEADER_CRC);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_unsupported_format_and_flags_are_rejected(void)
{
    char format_path[32] = {0};
    char flags_path[32] = {0};
    flash_t *format_flash = NULL;
    flash_t *flags_flash = NULL;
    uint8_t format_header[IMAGE_HEADER_SIZE];
    uint8_t flags_header[IMAGE_HEADER_SIZE];
    image_info_t info;
    bool ok = true;

    CHECK(make_flash_path(format_path));
    CHECK(make_flash_path(flags_path));
    CHECK(create_open_flash(format_path, &format_flash));
    CHECK(create_open_flash(flags_path, &flags_flash));
    CHECK(image_build_uncommitted_header(
        format_header,
        5U,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)) == IMAGE_OK);
    CHECK(image_build_uncommitted_header(
        flags_header,
        5U,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)) == IMAGE_OK);

    store_le32(format_header + IMAGE_FORMAT_OFFSET, IMAGE_FORMAT_VERSION + 1U);
    update_header_crc(format_header);
    CHECK(program_uncommitted_image(
        format_flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        format_header,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)));
    CHECK(image_publish_commit_marker(
        format_flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A]) == IMAGE_OK);
    CHECK(image_validate(
        format_flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        &info) == IMAGE_ERR_UNSUPPORTED_FORMAT);

    store_le32(flags_header + IMAGE_FLAGS_OFFSET, 1U);
    update_header_crc(flags_header);
    CHECK(program_uncommitted_image(
        flags_flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        flags_header,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)));
    CHECK(image_publish_commit_marker(
        flags_flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A]) == IMAGE_OK);
    CHECK(image_validate(
        flags_flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        &info) == IMAGE_ERR_UNSUPPORTED_FLAGS);

cleanup:
    flash_close(format_flash);
    flash_close(flags_flash);
    (void)unlink(format_path);
    (void)unlink(flags_path);
    return ok;
}

static bool test_out_of_bounds_payload_size_is_rejected_before_hashing(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    uint8_t header[IMAGE_HEADER_SIZE];
    image_info_t info;
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(create_open_flash(path, &flash));
    CHECK(image_build_uncommitted_header(
        header,
        5U,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)) == IMAGE_OK);
    store_le32(header + 16U, (uint32_t)TEST_SLOTS.slots[BOOT_SLOT_A].size);
    update_header_crc(header);
    CHECK(program_uncommitted_image(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        header,
        NULL,
        0U));
    CHECK(image_publish_commit_marker(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A]) == IMAGE_OK);
    CHECK(image_validate(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        &info) == IMAGE_ERR_INVALID_SIZE);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_payload_corruption_is_rejected(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    const uint8_t corrupt_unit[4] = {0xFEU, 0x10U, 0x22U, 0x34U};
    image_info_t info;
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(create_open_flash(path, &flash));
    CHECK(provision_committed_image(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        6U,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)));
    CHECK(flash_program(
        flash,
        TEST_SLOTS.slots[BOOT_SLOT_A].base_address + IMAGE_HEADER_SIZE,
        corrupt_unit,
        sizeof(corrupt_unit)) == FLASH_OK);
    CHECK(image_validate(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        &info) == IMAGE_ERR_HASH_MISMATCH);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_payload_larger_than_read_chunk_is_fully_hashed(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    uint8_t payload[600];
    image_info_t info;
    size_t index;
    bool ok = true;

    for (index = 0U; index < sizeof(payload); ++index) {
        payload[index] = (uint8_t)(index * 37U + 11U);
    }

    CHECK(make_flash_path(path));
    CHECK(create_open_flash(path, &flash));
    CHECK(provision_committed_image(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        99U,
        payload,
        sizeof(payload)));
    CHECK(image_validate(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        &info) == IMAGE_OK);
    CHECK(info.payload_size == sizeof(payload));
    CHECK(info.firmware_version == 99U);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_valid_header_with_different_payload_is_rejected(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    uint8_t header[IMAGE_HEADER_SIZE];
    uint8_t different_payload[sizeof(TEST_PAYLOAD)];
    image_info_t info;
    bool ok = true;

    (void)memset(different_payload, 0x00, sizeof(different_payload));
    CHECK(make_flash_path(path));
    CHECK(create_open_flash(path, &flash));
    CHECK(image_build_uncommitted_header(
        header,
        7U,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)) == IMAGE_OK);
    CHECK(program_uncommitted_image(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        header,
        different_payload,
        sizeof(different_payload)));
    CHECK(image_publish_commit_marker(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A]) == IMAGE_OK);
    CHECK(image_validate(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_A],
        &info) == IMAGE_ERR_HASH_MISMATCH);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_invalid_slot_bounds_fail_closed(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    firmware_slots_t invalid_slots = TEST_SLOTS;
    boot_decision_t decision;
    bool ok = true;

    invalid_slots.slots[BOOT_SLOT_A].base_address = 4000U;
    invalid_slots.slots[BOOT_SLOT_A].size = 512U;
    CHECK(make_flash_path(path));
    CHECK(create_open_flash(path, &flash));
    CHECK(boot_select_confirmed(
        flash,
        &invalid_slots,
        BOOT_SLOT_A,
        &decision) == BOOT_NO_BOOTABLE_IMAGE);
    CHECK(decision.selected_slot == BOOT_SLOT_NONE);
    CHECK(decision.validation_status == IMAGE_ERR_SLOT_BOUNDS);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_boot_does_not_guess_valid_inactive_slot(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    boot_decision_t decision;
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(create_open_flash(path, &flash));
    CHECK(provision_committed_image(
        flash,
        &TEST_SLOTS.slots[BOOT_SLOT_B],
        8U,
        TEST_PAYLOAD,
        sizeof(TEST_PAYLOAD)));
    CHECK(boot_select_confirmed(
        flash,
        &TEST_SLOTS,
        BOOT_SLOT_A,
        &decision) == BOOT_NO_BOOTABLE_IMAGE);
    CHECK(decision.selected_slot == BOOT_SLOT_NONE);
    CHECK(decision.validation_status == IMAGE_ERR_UNCOMMITTED);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_powered_off_flash_reports_storage_error(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    const uint8_t bytes[4] = {0U, 0U, 0U, 0U};
    boot_decision_t decision;
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(create_open_flash(path, &flash));
    CHECK(flash_inject_power_failure_after(flash, 0U) == FLASH_OK);
    CHECK(flash_program(flash, 0U, bytes, sizeof(bytes)) ==
          FLASH_ERR_POWER_LOSS);
    CHECK(boot_select_confirmed(
        flash,
        &TEST_SLOTS,
        BOOT_SLOT_A,
        &decision) == BOOT_STORAGE_ERROR);
    CHECK(decision.selected_slot == BOOT_SLOT_NONE);
    CHECK(decision.validation_status == IMAGE_ERR_POWER_LOSS);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

typedef bool (*test_function_t)(void);

typedef struct {
    const char *name;
    test_function_t function;
} test_case_t;

int main(void)
{
    const test_case_t tests[] = {
        {"CRC-32 known vector", test_crc32_known_vector},
        {"SHA-256 known vectors", test_sha256_known_vectors},
        {"valid confirmed A selected", test_valid_confirmed_a_is_selected},
        {"uncommitted image rejected", test_uncommitted_image_is_rejected},
        {"partial commit rejected", test_partial_commit_marker_is_rejected_after_reopen},
        {"complete commit survives lost return", test_complete_commit_is_accepted_when_caller_loses_power},
        {"bad magic rejected", test_bad_magic_is_rejected},
        {"bad header CRC rejected", test_bad_header_crc_is_rejected},
        {"unsupported format and flags", test_unsupported_format_and_flags_are_rejected},
        {"out-of-bounds size rejected", test_out_of_bounds_payload_size_is_rejected_before_hashing},
        {"payload corruption rejected", test_payload_corruption_is_rejected},
        {"multi-chunk payload hashed", test_payload_larger_than_read_chunk_is_fully_hashed},
        {"header-only validation rejected", test_valid_header_with_different_payload_is_rejected},
        {"invalid slot bounds", test_invalid_slot_bounds_fail_closed},
        {"boot does not guess inactive slot", test_boot_does_not_guess_valid_inactive_slot},
        {"powered-off flash storage error", test_powered_off_flash_reports_storage_error},
    };
    size_t index;
    size_t passed = 0U;

    (void)setvbuf(stdout, NULL, _IONBF, 0U);

    for (index = 0U; index < ARRAY_LENGTH(tests); ++index) {
        const bool ok = tests[index].function();
        printf("[%s] %s\n", ok ? "PASS" : "FAIL", tests[index].name);
        if (ok) {
            passed += 1U;
        }
    }

    printf("%zu/%zu image tests passed\n", passed, ARRAY_LENGTH(tests));
    return passed == ARRAY_LENGTH(tests) ? 0 : 1;
}
