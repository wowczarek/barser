/* BSD 3-Clause License
 *
 * Copyright (c) 2008 Google Inc.
 *               Protocol Buffers - Google's data interchange format
 *               https://developers.google.com/protocol-buffers/
 * Copyright (c) 2016 Arturo Martin-de-Nicolas
 *               https://github.com/amdn/itoa_ljust/
 * Copyright (c) 2018 Wojciech Owczarek
 *               https://github.com/wowczarek/barser/
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
 * @file   itoa.c
 * @date   Fri Sep 14 23:27:00 2018
 *
 * @brief  A fast int32_t / uint32_t itoa implementation based on work by
 *         Arturo Martin-de-Nicolas and Google.
 *
 */

#include <stdint.h>
#include <string.h>
#include "itoa.h"

/* Letters Unnecessary Table */
static const char lut100[100][2] = {
    "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19",
    "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32", "33", "34", "35", "36", "37", "38", "39",
    "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "50", "51", "52", "53", "54", "55", "56", "57", "58", "59",
    "60", "61", "62", "63", "64", "65", "66", "67", "68", "69", "70", "71", "72", "73", "74", "75", "76", "77", "78", "79",
    "80", "81", "82", "83", "84", "85", "86", "87", "88", "89", "90", "91", "92", "93", "94", "95", "96", "97", "98", "99"
};

/* put '00' .. '99' into buffer...  */
static inline char* put2(unsigned u, char* out) {
    memcpy(out, &lut100[u][0], 2);
    return out + 2;
}

/* convert an even-digit integer sequence to string in two-digit groups */
static inline void _itoa(uint32_t u, char **p, int d, int n) {

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

    /* a null-terminated string is a happy string */
    *(*p) = '\0';

}


/* unsigned version */
char* u32toa(char* out, const uint32_t in) {

    int d = 0;
    int n;

    /* handle first digit if odd number of digits and establish number of digits */
         if (in >= 100000000) n = (in < 1000000000) ? d = in / 100000000, *out++ = '0' + d, 9 : 10;
    else if (in <        100) n = (in < 10)         ? d = in            , *out++ = '0' + d, 1 : 2;
    else if (in <      10000) n = (in < 1000)       ? d = in / 100      , *out++ = '0' + d, 3 : 4;
    else if (in <    1000000) n = (in < 100000)     ? d = in / 10000    , *out++ = '0' + d, 5 : 6;
    else                      n = (in < 10000000)   ? d = in / 1000000  , *out++ = '0' + d, 7 : 8;

    /* handle remaining digits */
    _itoa(in, &out, d, n);

    /* return the NULL-termination spot. this allows us to progress in a buffer */
    return out;

}

/* signed version - just put a minus in front if negative, handle rest as normal */
char* i32toa(char *out, const int32_t in) {

        uint32_t u = in;

        if (in < 0) {
            *out++ = '-';
            u = -u;
        }

        return u32toa(out, u);
}
