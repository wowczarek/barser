/* BSD 3-Clause License
 *
 * Copyright (c) 2008 Google Inc.
*                Protocol Buffers - Google's data interchange format
 *               https://developers.google.com/protocol-buffers/
 * Copyright (c) 2016 Arturo Martin-de-Nicolas
 *               https://github.com/amdn/itoa_ljust/
 * Copyright (c) 2018 Wojciech Owczarek
 *
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
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
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
 * @file   int32toa.c
 * @date   Fri Sep 14 23:27:00 2018
 *
 * @brief  A fast int32_t / uint32_t implementation based on work by
 *         Arturo Martin-de-Nicolas and Google.
 *
 */

#include <stdint.h>
#include <string.h>
#include "itoa.h"

static const char lut100[201] =
	"0001020304050607080910111213141516171819"
	"2021222324252627282930313233343536373839"
	"4041424344454647484950515253545556575859"
	"6061626364656667686970717273747576777879"
	"8081828384858687888990919293949596979899";

static inline char* put2(unsigned u, char* out) {
/*    memcpy(out, &((uint16_t*)lut100)[u], 2); */
    memcpy(out, &lut100[u * 2], 2);
    return out + 2;
}

static inline int digits( uint32_t in, unsigned base, int *digit, char** out, int n ) {

    if (in < base * 10) {
	*digit = in / base;
	*(*out)++ = '0' + *digit;
	--n;
    }

    return n;
}

static inline void itoa(uint32_t u, char **p, int d, int n) {
    switch(n) {
	case 10: d  = u / 100000000; *p = put2(d, *p);
	case  9: u -= d * 100000000;
	case  8: d  = u /   1000000; *p = put2(d, *p);
	case  7: u -= d *   1000000;
	case  6: d  = u /     10000; *p = put2(d, *p);
	case  5: u -= d *     10000;
	case  4: d  = u /       100; *p = put2(d, *p);
	case  3: u -= d *       100;
	case  2: d  = u /         1; *p = put2(d, *p);
	case  1: ;
    }

    *(*p) = '\0';

}


char* u32toa(char* out, const uint32_t in) {

    char* start = out;
    int d = 0;
    int n;

	if (in >=100000000)  n = digits(in, 100000000, &d, &out, 10);
    else if (in <       100) n = digits(in,         1, &d, &out,  2);
    else if (in <     10000) n = digits(in,       100, &d, &out,  4);
    else if (in <   1000000) n = digits(in,     10000, &d, &out,  6);
    else                     n = digits(in,   1000000, &d, &out,  8);

    itoa(in, &out, d, n);

    return start;

}

/*
    char* itoa(int32_t i, char* p) {
        uint32_t u = i;
        if (i < 0) {
            *p++ = '-';
            u = -u;
        }
        return itoa(u, p);
    }
*/
