#ifndef PLS_BOOT_SELECTOR_H
#define PLS_BOOT_SELECTOR_H

#include "flash_sim.h"
#include "image.h"

typedef enum {
    BOOT_SLOT_NONE = -1,
    BOOT_SLOT_A = 0,
    BOOT_SLOT_B = 1
} boot_slot_id_t;

typedef struct {
    image_slot_t slots[2];
} firmware_slots_t;

typedef enum {
    BOOT_OK = 0,
    BOOT_NO_BOOTABLE_IMAGE,
    BOOT_STORAGE_ERROR,
    BOOT_INVALID_ARGUMENT
} boot_status_t;

typedef struct {
    boot_slot_id_t selected_slot;
    image_info_t image;
    image_status_t validation_status;
} boot_decision_t;

boot_status_t boot_select_confirmed(
    flash_t *flash,
    const firmware_slots_t *slots,
    boot_slot_id_t confirmed_slot,
    boot_decision_t *out_decision);

const char *boot_status_string(boot_status_t status);

#endif
