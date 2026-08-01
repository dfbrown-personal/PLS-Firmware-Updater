#include "flash_sim.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define FORMAT_BUFFER_SIZE 4096U

struct flash {
    int fd;
    flash_geometry_t geometry;
    bool power_lost;
    bool failure_enabled;
    uint64_t effects_until_failure;
    uint64_t persisted_effect_count;
};

static bool geometry_is_valid(const flash_geometry_t *geometry)
{
    if (geometry == NULL) {
        return false;
    }

    if (geometry->total_size == 0U ||
        geometry->sector_size == 0U ||
        geometry->program_unit == 0U) {
        return false;
    }

    if (geometry->total_size > (uint64_t)INT64_MAX ||
        geometry->total_size % geometry->sector_size != 0U ||
        geometry->sector_size % geometry->program_unit != 0U) {
        return false;
    }

    return true;
}

static bool range_is_valid(
    const flash_geometry_t *geometry,
    uint64_t address,
    size_t length)
{
    const uint64_t length_u64 = (uint64_t)length;

    if (address > geometry->total_size) {
        return false;
    }

    return length_u64 <= geometry->total_size - address;
}

static flash_status_t read_exact(
    int fd,
    uint64_t address,
    void *buffer,
    size_t length)
{
    uint8_t *cursor = buffer;
    size_t remaining = length;
    off_t offset = (off_t)address;

    while (remaining > 0U) {
        const ssize_t count = pread(fd, cursor, remaining, offset);

        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return FLASH_ERR_IO;
        }

        if (count == 0) {
            return FLASH_ERR_IO;
        }

        cursor += (size_t)count;
        remaining -= (size_t)count;
        offset += count;
    }

    return FLASH_OK;
}

static flash_status_t write_exact(
    int fd,
    uint64_t address,
    const void *buffer,
    size_t length)
{
    const uint8_t *cursor = buffer;
    size_t remaining = length;
    off_t offset = (off_t)address;

    while (remaining > 0U) {
        const ssize_t count = pwrite(fd, cursor, remaining, offset);

        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return FLASH_ERR_IO;
        }

        if (count == 0) {
            return FLASH_ERR_IO;
        }

        cursor += (size_t)count;
        remaining -= (size_t)count;
        offset += count;
    }

    return FLASH_OK;
}

static flash_status_t require_power(const flash_t *flash)
{
    if (flash == NULL) {
        return FLASH_ERR_INVALID_ARGUMENT;
    }

    return flash->power_lost ? FLASH_ERR_POWER_LOSS : FLASH_OK;
}

static flash_status_t fail_before_effect_if_needed(flash_t *flash)
{
    if (flash->failure_enabled && flash->effects_until_failure == 0U) {
        flash->power_lost = true;
        return FLASH_ERR_POWER_LOSS;
    }

    return FLASH_OK;
}

static flash_status_t sync_persistent_state(const flash_t *flash)
{
    return fsync(flash->fd) == 0 ? FLASH_OK : FLASH_ERR_IO;
}

static flash_status_t persist_byte(
    flash_t *flash,
    uint64_t address,
    uint8_t value)
{
    flash_status_t status = fail_before_effect_if_needed(flash);

    if (status != FLASH_OK) {
        return status;
    }

    status = write_exact(flash->fd, address, &value, sizeof(value));
    if (status != FLASH_OK) {
        return status;
    }

    flash->persisted_effect_count += 1U;

    if (flash->failure_enabled) {
        flash->effects_until_failure -= 1U;
        if (flash->effects_until_failure == 0U) {
            status = sync_persistent_state(flash);
            if (status != FLASH_OK) {
                return status;
            }
            flash->power_lost = true;
            return FLASH_ERR_POWER_LOSS;
        }
    }

    return FLASH_OK;
}

flash_status_t flash_format(const char *path, const flash_geometry_t *geometry)
{
    uint8_t erased[FORMAT_BUFFER_SIZE];
    uint64_t offset = 0U;
    int fd;

    if (path == NULL) {
        return FLASH_ERR_INVALID_ARGUMENT;
    }

    if (!geometry_is_valid(geometry)) {
        return FLASH_ERR_INVALID_GEOMETRY;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        return FLASH_ERR_IO;
    }

    memset(erased, 0xFF, sizeof(erased));

    while (offset < geometry->total_size) {
        const uint64_t remaining = geometry->total_size - offset;
        const size_t chunk = remaining < sizeof(erased)
                                 ? (size_t)remaining
                                 : sizeof(erased);
        const flash_status_t status = write_exact(fd, offset, erased, chunk);

        if (status != FLASH_OK) {
            (void)close(fd);
            return status;
        }

        offset += (uint64_t)chunk;
    }

    if (fsync(fd) != 0) {
        (void)close(fd);
        return FLASH_ERR_IO;
    }

    if (close(fd) != 0) {
        return FLASH_ERR_IO;
    }

    return FLASH_OK;
}

flash_status_t flash_open(
    flash_t **out_flash,
    const char *path,
    const flash_geometry_t *geometry)
{
    struct stat file_info;
    flash_t *flash;
    int fd;

    if (out_flash == NULL || path == NULL) {
        return FLASH_ERR_INVALID_ARGUMENT;
    }

    *out_flash = NULL;

    if (!geometry_is_valid(geometry)) {
        return FLASH_ERR_INVALID_GEOMETRY;
    }

    fd = open(path, O_RDWR);
    if (fd < 0) {
        return FLASH_ERR_IO;
    }

    if (fstat(fd, &file_info) != 0) {
        (void)close(fd);
        return FLASH_ERR_IO;
    }

    if (file_info.st_size < 0 ||
        (uint64_t)file_info.st_size != geometry->total_size) {
        (void)close(fd);
        return FLASH_ERR_WRONG_FILE_SIZE;
    }

    flash = calloc(1U, sizeof(*flash));
    if (flash == NULL) {
        (void)close(fd);
        return FLASH_ERR_IO;
    }

    flash->fd = fd;
    flash->geometry = *geometry;
    *out_flash = flash;
    return FLASH_OK;
}

void flash_close(flash_t *flash)
{
    if (flash == NULL) {
        return;
    }

    (void)close(flash->fd);
    free(flash);
}

flash_status_t flash_read(
    flash_t *flash,
    uint64_t address,
    void *buffer,
    size_t length)
{
    flash_status_t status = require_power(flash);

    if (status != FLASH_OK) {
        return status;
    }

    if (length > 0U && buffer == NULL) {
        return FLASH_ERR_INVALID_ARGUMENT;
    }

    if (!range_is_valid(&flash->geometry, address, length)) {
        return FLASH_ERR_OUT_OF_RANGE;
    }

    if (length == 0U) {
        return FLASH_OK;
    }

    return read_exact(flash->fd, address, buffer, length);
}

flash_status_t flash_program(
    flash_t *flash,
    uint64_t address,
    const void *data,
    size_t length)
{
    const uint8_t *source = data;
    size_t index;
    flash_status_t status = require_power(flash);

    if (status != FLASH_OK) {
        return status;
    }

    if (data == NULL || length == 0U) {
        return FLASH_ERR_INVALID_ARGUMENT;
    }

    if (!range_is_valid(&flash->geometry, address, length)) {
        return FLASH_ERR_OUT_OF_RANGE;
    }

    if (address % flash->geometry.program_unit != 0U ||
        (uint64_t)length % flash->geometry.program_unit != 0U) {
        return FLASH_ERR_UNALIGNED;
    }

    /* Validate the complete request before changing persistent state. */
    for (index = 0U; index < length; ++index) {
        uint8_t current;

        status = read_exact(flash->fd, address + (uint64_t)index, &current, 1U);
        if (status != FLASH_OK) {
            return status;
        }

        if ((current & source[index]) != source[index]) {
            return FLASH_ERR_ILLEGAL_TRANSITION;
        }
    }

    for (index = 0U; index < length; ++index) {
        status = persist_byte(
            flash,
            address + (uint64_t)index,
            source[index]);
        if (status != FLASH_OK) {
            return status;
        }
    }

    return sync_persistent_state(flash);
}

flash_status_t flash_erase_sector(flash_t *flash, uint64_t sector_address)
{
    uint64_t index;
    flash_status_t status = require_power(flash);

    if (status != FLASH_OK) {
        return status;
    }

    if (sector_address % flash->geometry.sector_size != 0U) {
        return FLASH_ERR_UNALIGNED;
    }

    if (!range_is_valid(
            &flash->geometry,
            sector_address,
            (size_t)flash->geometry.sector_size)) {
        return FLASH_ERR_OUT_OF_RANGE;
    }

    for (index = 0U; index < flash->geometry.sector_size; ++index) {
        status = persist_byte(flash, sector_address + index, 0xFFU);
        if (status != FLASH_OK) {
            return status;
        }
    }

    return sync_persistent_state(flash);
}

flash_status_t flash_inject_power_failure_after(
    flash_t *flash,
    uint64_t byte_count)
{
    flash_status_t status = require_power(flash);

    if (status != FLASH_OK) {
        return status;
    }

    flash->failure_enabled = true;
    flash->effects_until_failure = byte_count;
    return FLASH_OK;
}

flash_status_t flash_disable_power_failure(flash_t *flash)
{
    flash_status_t status = require_power(flash);

    if (status != FLASH_OK) {
        return status;
    }

    flash->failure_enabled = false;
    flash->effects_until_failure = 0U;
    return FLASH_OK;
}

bool flash_has_lost_power(const flash_t *flash)
{
    return flash != NULL && flash->power_lost;
}

uint64_t flash_persisted_effect_count(const flash_t *flash)
{
    return flash == NULL ? 0U : flash->persisted_effect_count;
}

const flash_geometry_t *flash_geometry(const flash_t *flash)
{
    return flash == NULL ? NULL : &flash->geometry;
}

const char *flash_status_string(flash_status_t status)
{
    switch (status) {
    case FLASH_OK:
        return "ok";
    case FLASH_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case FLASH_ERR_INVALID_GEOMETRY:
        return "invalid geometry";
    case FLASH_ERR_OUT_OF_RANGE:
        return "out of range";
    case FLASH_ERR_UNALIGNED:
        return "unaligned operation";
    case FLASH_ERR_ILLEGAL_TRANSITION:
        return "illegal 0-to-1 program transition";
    case FLASH_ERR_WRONG_FILE_SIZE:
        return "backing file has wrong size";
    case FLASH_ERR_IO:
        return "host I/O failure";
    case FLASH_ERR_POWER_LOSS:
        return "simulated power loss";
    default:
        return "unknown flash status";
    }
}
