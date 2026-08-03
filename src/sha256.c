#include "sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const uint32_t ROUND_CONSTANTS[64] = {
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
    0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
    0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
    0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
    0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
    0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
    0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
    0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
    0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
    0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
    0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
    0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
    0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
    0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
    0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
    0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
};

static uint32_t rotate_right(uint32_t value, unsigned int count)
{
    return (value >> count) | (value << (32U - count));
}

static uint32_t load_be32(const uint8_t bytes[static 4])
{
    return ((uint32_t)bytes[0] << 24U) |
           ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) |
           (uint32_t)bytes[3];
}

static void store_be32(uint8_t bytes[static 4], uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
}

static void store_be64(uint8_t bytes[static 8], uint64_t value)
{
    bytes[0] = (uint8_t)(value >> 56U);
    bytes[1] = (uint8_t)(value >> 48U);
    bytes[2] = (uint8_t)(value >> 40U);
    bytes[3] = (uint8_t)(value >> 32U);
    bytes[4] = (uint8_t)(value >> 24U);
    bytes[5] = (uint8_t)(value >> 16U);
    bytes[6] = (uint8_t)(value >> 8U);
    bytes[7] = (uint8_t)value;
}

static void transform(sha256_context_t *context, const uint8_t block[64])
{
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    size_t index;

    for (index = 0U; index < 16U; ++index) {
        words[index] = load_be32(block + (index * 4U));
    }

    for (index = 16U; index < 64U; ++index) {
        const uint32_t s0 = rotate_right(words[index - 15U], 7U) ^
                            rotate_right(words[index - 15U], 18U) ^
                            (words[index - 15U] >> 3U);
        const uint32_t s1 = rotate_right(words[index - 2U], 17U) ^
                            rotate_right(words[index - 2U], 19U) ^
                            (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (index = 0U; index < 64U; ++index) {
        const uint32_t sum1 = rotate_right(e, 6U) ^
                              rotate_right(e, 11U) ^
                              rotate_right(e, 25U);
        const uint32_t choose = (e & f) ^ ((~e) & g);
        const uint32_t temporary1 = h + sum1 + choose +
                                    ROUND_CONSTANTS[index] + words[index];
        const uint32_t sum0 = rotate_right(a, 2U) ^
                              rotate_right(a, 13U) ^
                              rotate_right(a, 22U);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temporary2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void sha256_init(sha256_context_t *context)
{
    if (context == NULL) {
        return;
    }

    context->state[0] = 0x6A09E667U;
    context->state[1] = 0xBB67AE85U;
    context->state[2] = 0x3C6EF372U;
    context->state[3] = 0xA54FF53AU;
    context->state[4] = 0x510E527FU;
    context->state[5] = 0x9B05688CU;
    context->state[6] = 0x1F83D9ABU;
    context->state[7] = 0x5BE0CD19U;
    context->total_size = 0U;
    context->block_size = 0U;
}

void sha256_update(sha256_context_t *context, const void *data, size_t length)
{
    const uint8_t *bytes = data;

    if (context == NULL || (data == NULL && length != 0U)) {
        return;
    }

    context->total_size += (uint64_t)length;

    while (length > 0U) {
        const size_t available = SHA256_BLOCK_SIZE - context->block_size;
        const size_t chunk = length < available ? length : available;

        (void)memcpy(context->block + context->block_size, bytes, chunk);
        context->block_size += chunk;
        bytes += chunk;
        length -= chunk;

        if (context->block_size == SHA256_BLOCK_SIZE) {
            transform(context, context->block);
            context->block_size = 0U;
        }
    }
}

void sha256_final(sha256_context_t *context, uint8_t digest[SHA256_DIGEST_SIZE])
{
    const uint64_t bit_length = context == NULL ? 0U : context->total_size * 8U;
    size_t index;

    if (context == NULL || digest == NULL) {
        return;
    }

    context->block[context->block_size++] = 0x80U;

    if (context->block_size > 56U) {
        (void)memset(
            context->block + context->block_size,
            0,
            SHA256_BLOCK_SIZE - context->block_size);
        transform(context, context->block);
        context->block_size = 0U;
    }

    (void)memset(context->block + context->block_size, 0, 56U - context->block_size);
    store_be64(context->block + 56U, bit_length);
    transform(context, context->block);

    for (index = 0U; index < 8U; ++index) {
        store_be32(digest + (index * 4U), context->state[index]);
    }

    context->block_size = 0U;
}

void sha256_compute(
    const void *data,
    size_t length,
    uint8_t digest[SHA256_DIGEST_SIZE])
{
    sha256_context_t context;

    sha256_init(&context);
    sha256_update(&context, data, length);
    sha256_final(&context, digest);
}
