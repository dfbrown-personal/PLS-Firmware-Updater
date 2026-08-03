#include "crc32.h"

#include <stddef.h>
#include <stdint.h>

uint32_t crc32_compute(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    uint32_t crc = UINT32_MAX;
    size_t index;

    if (data == NULL && length != 0U) {
        return 0U;
    }

    for (index = 0U; index < length; ++index) {
        unsigned int bit;

        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }

    return ~crc;
}
