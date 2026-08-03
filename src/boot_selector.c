#include "boot_selector.h"

#include "image.h"

#include <string.h>

boot_status_t boot_select_confirmed(
    flash_t *flash,
    const firmware_slots_t *slots,
    boot_slot_id_t confirmed_slot,
    boot_decision_t *out_decision)
{
    image_status_t image_status;

    if (flash == NULL || slots == NULL || out_decision == NULL ||
        (confirmed_slot != BOOT_SLOT_A && confirmed_slot != BOOT_SLOT_B)) {
        return BOOT_INVALID_ARGUMENT;
    }

    (void)memset(out_decision, 0, sizeof(*out_decision));
    out_decision->selected_slot = BOOT_SLOT_NONE;
    image_status = image_validate(
        flash,
        &slots->slots[(size_t)confirmed_slot],
        &out_decision->image);
    out_decision->validation_status = image_status;

    if (image_status == IMAGE_OK) {
        out_decision->selected_slot = confirmed_slot;
        return BOOT_OK;
    }

    if (image_status == IMAGE_ERR_FLASH || image_status == IMAGE_ERR_POWER_LOSS) {
        return BOOT_STORAGE_ERROR;
    }

    return BOOT_NO_BOOTABLE_IMAGE;
}

const char *boot_status_string(boot_status_t status)
{
    switch (status) {
    case BOOT_OK:
        return "ok";
    case BOOT_NO_BOOTABLE_IMAGE:
        return "no bootable confirmed image";
    case BOOT_STORAGE_ERROR:
        return "storage error during boot selection";
    case BOOT_INVALID_ARGUMENT:
        return "invalid boot-selection argument";
    default:
        return "unknown boot status";
    }
}
