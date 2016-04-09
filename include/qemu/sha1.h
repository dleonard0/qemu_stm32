/*
 *  SHA-1 hash
 *
 *  Public domain
 *
 *  Authors:
 *   Steve Reid <steve@edmweb.com>
 *
 */

#ifndef QEMU_SHA1_H
#define QEMU_SHA1_H

#include "qemu-common.h"

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t  buffer[64];
} SHA1_CTX;

void SHA1Init(SHA1_CTX *context);
void SHA1Update(SHA1_CTX *context, const void *data, size_t len);
void SHA1Final(char digest[static 20], SHA1_CTX *context);

#endif
