#ifndef PLS_CRC32_H
#define PLS_CRC32_H

#include <stddef.h>
#include <stdint.h>

uint32_t crc32_compute(const void *data, size_t length);

#endif
