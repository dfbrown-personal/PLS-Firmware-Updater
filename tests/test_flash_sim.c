#include "flash_sim.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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
    .total_size = 64U,
    .sector_size = 16U,
    .program_unit = 4U,
};

static bool make_flash_path(char path[static 32])
{
    int fd;

    (void)strcpy(path, "/tmp/pls-flash-test-XXXXXX");
    fd = mkstemp(path);
    if (fd < 0) {
        return false;
    }

    return close(fd) == 0;
}

static bool bytes_equal(const uint8_t *bytes, size_t length, uint8_t expected)
{
    size_t index;

    for (index = 0U; index < length; ++index) {
        if (bytes[index] != expected) {
            return false;
        }
    }

    return true;
}

static bool test_geometry_and_backing_size_are_validated(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    const flash_geometry_t zero_size = {
        .total_size = 0U,
        .sector_size = 16U,
        .program_unit = 4U,
    };
    const flash_geometry_t partial_sector = {
        .total_size = 63U,
        .sector_size = 16U,
        .program_unit = 4U,
    };
    const flash_geometry_t wrong_file_size = {
        .total_size = 32U,
        .sector_size = 16U,
        .program_unit = 4U,
    };
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(flash_format(path, &zero_size) == FLASH_ERR_INVALID_GEOMETRY);
    CHECK(flash_format(path, &partial_sector) == FLASH_ERR_INVALID_GEOMETRY);
    CHECK(flash_format(path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_open(&flash, path, &wrong_file_size) ==
          FLASH_ERR_WRONG_FILE_SIZE);
    CHECK(flash == NULL);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_format_is_erased_and_persistent(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    uint8_t bytes[64];
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(flash_format(path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_open(&flash, path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_read(flash, 0U, bytes, sizeof(bytes)) == FLASH_OK);
    CHECK(bytes_equal(bytes, sizeof(bytes), 0xFFU));

    flash_close(flash);
    flash = NULL;
    CHECK(flash_open(&flash, path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_read(flash, 0U, bytes, sizeof(bytes)) == FLASH_OK);
    CHECK(bytes_equal(bytes, sizeof(bytes), 0xFFU));

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_program_allows_only_one_to_zero(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    const uint8_t first[4] = {0xF0U, 0x0FU, 0xAAU, 0x55U};
    const uint8_t illegal[4] = {0xFFU, 0x0FU, 0xAAU, 0x55U};
    uint8_t actual[4];
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(flash_format(path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_open(&flash, path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_program(flash, 0U, first, sizeof(first)) == FLASH_OK);
    CHECK(flash_program(flash, 0U, illegal, sizeof(illegal)) ==
          FLASH_ERR_ILLEGAL_TRANSITION);
    CHECK(flash_read(flash, 0U, actual, sizeof(actual)) == FLASH_OK);
    CHECK(memcmp(actual, first, sizeof(actual)) == 0);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_alignment_and_bounds_are_enforced(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    const uint8_t bytes[8] = {0U};
    uint8_t output[2];
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(flash_format(path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_open(&flash, path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_program(flash, 2U, bytes, 4U) == FLASH_ERR_UNALIGNED);
    CHECK(flash_program(flash, 0U, bytes, 6U) == FLASH_ERR_UNALIGNED);
    CHECK(flash_program(flash, 60U, bytes, sizeof(bytes)) ==
          FLASH_ERR_OUT_OF_RANGE);
    CHECK(flash_read(flash, 63U, output, sizeof(output)) ==
          FLASH_ERR_OUT_OF_RANGE);
    CHECK(flash_erase_sector(flash, 4U) == FLASH_ERR_UNALIGNED);
    CHECK(flash_erase_sector(flash, 64U) == FLASH_ERR_OUT_OF_RANGE);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_sector_erase_does_not_touch_neighbor(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    uint8_t programmed[32];
    uint8_t actual[32];
    bool ok = true;

    (void)memset(programmed, 0x00, sizeof(programmed));
    CHECK(make_flash_path(path));
    CHECK(flash_format(path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_open(&flash, path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_program(flash, 0U, programmed, sizeof(programmed)) == FLASH_OK);
    CHECK(flash_erase_sector(flash, 0U) == FLASH_OK);
    CHECK(flash_read(flash, 0U, actual, sizeof(actual)) == FLASH_OK);
    CHECK(bytes_equal(actual, 16U, 0xFFU));
    CHECK(bytes_equal(actual + 16U, 16U, 0x00U));

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_partial_program_survives_reopen(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    uint8_t programmed[8];
    uint8_t actual[8];
    bool ok = true;

    (void)memset(programmed, 0x00, sizeof(programmed));
    CHECK(make_flash_path(path));
    CHECK(flash_format(path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_open(&flash, path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_inject_power_failure_after(flash, 3U) == FLASH_OK);
    CHECK(flash_program(flash, 0U, programmed, sizeof(programmed)) ==
          FLASH_ERR_POWER_LOSS);
    CHECK(flash_has_lost_power(flash));
    CHECK(flash_persisted_effect_count(flash) == 3U);
    CHECK(flash_read(flash, 0U, actual, sizeof(actual)) ==
          FLASH_ERR_POWER_LOSS);

    flash_close(flash);
    flash = NULL;
    CHECK(flash_open(&flash, path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_read(flash, 0U, actual, sizeof(actual)) == FLASH_OK);
    CHECK(bytes_equal(actual, 3U, 0x00U));
    CHECK(bytes_equal(actual + 3U, 5U, 0xFFU));

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_partial_erase_survives_reopen(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    uint8_t programmed[32];
    uint8_t actual[32];
    bool ok = true;

    (void)memset(programmed, 0x00, sizeof(programmed));
    CHECK(make_flash_path(path));
    CHECK(flash_format(path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_open(&flash, path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_program(flash, 0U, programmed, sizeof(programmed)) == FLASH_OK);
    CHECK(flash_inject_power_failure_after(flash, 5U) == FLASH_OK);
    CHECK(flash_erase_sector(flash, 0U) == FLASH_ERR_POWER_LOSS);

    flash_close(flash);
    flash = NULL;
    CHECK(flash_open(&flash, path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_read(flash, 0U, actual, sizeof(actual)) == FLASH_OK);
    CHECK(bytes_equal(actual, 5U, 0xFFU));
    CHECK(bytes_equal(actual + 5U, 27U, 0x00U));

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_failure_after_final_effect_is_ambiguous_to_caller(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    const uint8_t programmed[4] = {0x12U, 0x34U, 0x56U, 0x78U};
    uint8_t actual[4];
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(flash_format(path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_open(&flash, path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_inject_power_failure_after(flash, sizeof(programmed)) == FLASH_OK);
    CHECK(flash_program(flash, 0U, programmed, sizeof(programmed)) ==
          FLASH_ERR_POWER_LOSS);

    flash_close(flash);
    flash = NULL;
    CHECK(flash_open(&flash, path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_read(flash, 0U, actual, sizeof(actual)) == FLASH_OK);
    CHECK(memcmp(actual, programmed, sizeof(actual)) == 0);

cleanup:
    flash_close(flash);
    (void)unlink(path);
    return ok;
}

static bool test_failure_before_first_effect_changes_nothing(void)
{
    char path[32] = {0};
    flash_t *flash = NULL;
    const uint8_t programmed[4] = {0U, 0U, 0U, 0U};
    uint8_t actual[4];
    bool ok = true;

    CHECK(make_flash_path(path));
    CHECK(flash_format(path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_open(&flash, path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_inject_power_failure_after(flash, 0U) == FLASH_OK);
    CHECK(flash_program(flash, 0U, programmed, sizeof(programmed)) ==
          FLASH_ERR_POWER_LOSS);

    flash_close(flash);
    flash = NULL;
    CHECK(flash_open(&flash, path, &TEST_GEOMETRY) == FLASH_OK);
    CHECK(flash_read(flash, 0U, actual, sizeof(actual)) == FLASH_OK);
    CHECK(bytes_equal(actual, sizeof(actual), 0xFFU));

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
        {"geometry and backing size", test_geometry_and_backing_size_are_validated},
        {"format is erased and persistent", test_format_is_erased_and_persistent},
        {"program permits only 1-to-0", test_program_allows_only_one_to_zero},
        {"alignment and bounds", test_alignment_and_bounds_are_enforced},
        {"sector erase isolation", test_sector_erase_does_not_touch_neighbor},
        {"partial program persistence", test_partial_program_survives_reopen},
        {"partial erase persistence", test_partial_erase_survives_reopen},
        {"failure after final effect", test_failure_after_final_effect_is_ambiguous_to_caller},
        {"failure before first effect", test_failure_before_first_effect_changes_nothing},
    };
    size_t index;
    size_t passed = 0U;

    (void)setvbuf(stdout, NULL, _IONBF, 0U);

    for (index = 0U; index < ARRAY_LENGTH(tests); ++index) {
        printf("[RUN ] %s\n", tests[index].name);
        const bool ok = tests[index].function();
        printf("[%s] %s\n", ok ? "PASS" : "FAIL", tests[index].name);
        if (ok) {
            passed += 1U;
        }
    }

    printf("%zu/%zu tests passed\n", passed, ARRAY_LENGTH(tests));
    return passed == ARRAY_LENGTH(tests) ? 0 : 1;
}
