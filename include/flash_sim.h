#ifndef PLS_FLASH_SIM_H
#define PLS_FLASH_SIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct flash flash_t;

typedef struct {
    uint64_t total_size;
    uint64_t sector_size;
    uint64_t program_unit;
} flash_geometry_t;

typedef enum {
    FLASH_OK = 0,
    FLASH_ERR_INVALID_ARGUMENT,
    FLASH_ERR_INVALID_GEOMETRY,
    FLASH_ERR_OUT_OF_RANGE,
    FLASH_ERR_UNALIGNED,
    FLASH_ERR_ILLEGAL_TRANSITION,
    FLASH_ERR_WRONG_FILE_SIZE,
    FLASH_ERR_IO,
    FLASH_ERR_POWER_LOSS
} flash_status_t;

/* Provisioning is outside the injected update fault model. */
flash_status_t flash_format(const char *path, const flash_geometry_t *geometry);

flash_status_t flash_open(
    flash_t **out_flash,
    const char *path,
    const flash_geometry_t *geometry);

void flash_close(flash_t *flash);

flash_status_t flash_read(
    flash_t *flash,
    uint64_t address,
    void *buffer,
    size_t length);

flash_status_t flash_program(
    flash_t *flash,
    uint64_t address,
    const void *data,
    size_t length);

flash_status_t flash_erase_sector(flash_t *flash, uint64_t sector_address);

/*
 * Fail after exactly byte_count persistent byte positions have been processed.
 * A count of zero fails before the first byte. The flash instance remains
 * powered off after the fault and must be closed and reopened to continue.
 */
flash_status_t flash_inject_power_failure_after(
    flash_t *flash,
    uint64_t byte_count);

flash_status_t flash_disable_power_failure(flash_t *flash);

bool flash_has_lost_power(const flash_t *flash);
uint64_t flash_persisted_effect_count(const flash_t *flash);
const flash_geometry_t *flash_geometry(const flash_t *flash);
const char *flash_status_string(flash_status_t status);

#endif
