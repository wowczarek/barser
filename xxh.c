/* BSD 2-Clause License
 *
 * Copyright (c) 2018, Wojciech Owczarek
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/**
 * @file   xxh.c
 * @date   Fri Sep 14 23:27:00 2018
 *
 * @brief  A simple 32-bit implemantation of xxHash by Yann Collet with no seed
 *         and no universal endianness.
 *
 */

#include <stdint.h>
#include <string.h>
#include "xxh.h"

/* magic primes */
#define XXH32_P1	0x9e3779b1 /* Buongiorno Signore Bonacci! */
#define XXH32_P2	0x85ebca77
#define XXH32_P3	0xc2b2ae3d
#define XXH32_P4	0x27d4eb2f
#define XXH32_P5	0x165667b1

/* rotate left. any decent compiler should turn this into rol (or ror on little-endians) */
#define rol32(var, pos) (((var) << pos) | ((var) >> (32 - pos)))

uint32_t xxHash(const void* in, const size_t len) {

    const unsigned char* marker = (const unsigned char*) in;
    const unsigned char* end = marker + len;
    uint32_t hash;

    if(len >= 16) {

	const unsigned char *lim = end - 16;
	uint32_t acc[4] = { XXH32_P1 + XXH32_P2, XXH32_P2, 0, -XXH32_P1 };

	do {
	    acc[0] += *(uint32_t*)marker * XXH32_P2; acc[0] = rol32(acc[0], 13); acc[0] *= XXH32_P1; marker += 4;
	    acc[1] += *(uint32_t*)marker * XXH32_P2; acc[1] = rol32(acc[1], 13); acc[1] *= XXH32_P1; marker += 4;
	    acc[2] += *(uint32_t*)marker * XXH32_P2; acc[2] = rol32(acc[2], 13); acc[2] *= XXH32_P1; marker += 4;
	    acc[3] += *(uint32_t*)marker * XXH32_P2; acc[3] = rol32(acc[3], 13); acc[3] *= XXH32_P1; marker += 4;
	} while (marker <= lim);

	hash = rol32(acc[0], 1) + rol32(acc[1], 7) + rol32(acc[2], 12) + rol32(acc[3], 18);

    } else {

	hash = XXH32_P5;

    }

    hash += (uint32_t) len;

    while(marker < end - 4) {

	hash += *(uint32_t*)marker * XXH32_P3;
	hash = rol32(hash, 17) * XXH32_P4;
	marker += 4;

    }

    while(marker <= end) {

	hash += *marker * XXH32_P5;
	hash = rol32(hash, 11) * XXH32_P1;
	marker++;

    }

    hash ^= hash >> 15;
    hash *= XXH32_P2;
    hash ^= hash >> 13;
    hash *= XXH32_P3;
    hash ^= hash >> 16;

    return hash;

}
