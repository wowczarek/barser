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
 * @file   duration.h
 * @date   Sun Apr15 13:40:12 2018
 *
 * @brief  Simple duration measurement and conversion macros
 *
 */

#ifndef __X_DURATION_H_
#define __X_DURATION_H_

#define _POSIX_C_SOURCE 199309L /* because clock_gettime */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#define HUMANTIME_WIDTH 31

enum {
	TUNIT_S = 0,
	TUNIT_MS,
	TUNIT_US,
	TUNIT_NS
};

/* helper data */
static const char _hunits[4][3] = { "s", "ms", "us", "ns"};
char _humantime[HUMANTIME_WIDTH];

/* basic duration measurement macros */

/* initialise a duration measurement instance of given name */
#define DUR_INIT(name) unsigned long long name##_delta; struct timespec name##_t1, name##_t2;
/* start duration measurement */
#define DUR_START(name) clock_gettime(CLOCK_MONOTONIC,&name##_t1);
/* end duration measurement */
#define DUR_END(name) clock_gettime(CLOCK_MONOTONIC,&name##_t2); name##_delta = (name##_t2.tv_sec * 1000000000 + name##_t2.tv_nsec) - (name##_t1.tv_sec * 1000000000 + name##_t1.tv_nsec);
/* print duration in nanoseconds with a message prefix*/
#define DUR_PRINT(name, msg) fprintf(stderr, "%s: %llu ns\n", msg, name##_delta);
/* end measurement and print duration as above */
#define DUR_EPRINT(name, msg) DUR_END(name); fprintf(stderr, "%s: %llu ns\n", msg, name##_delta);

/* get duration in human time */
#define DUR_HUMANTIME(var) ( memset(_humantime, 0, HUMANTIME_WIDTH),\
	    (void) ( (var > 1000000000) ? snprintf(_humantime, HUMANTIME_WIDTH, "%.09f %s", var / 1000000000.0, _hunits[TUNIT_S]) :\
	    (var > 1000000) ? snprintf(_humantime, HUMANTIME_WIDTH, "%.06f %s", var / 1000000.0, _hunits[TUNIT_MS]) :\
	    (var > 1000) ? snprintf(_humantime, HUMANTIME_WIDTH, "%.03f %s", var / 1000.0, _hunits[TUNIT_US]) :\
	    snprintf(_humantime, HUMANTIME_WIDTH, "%.0f %s", var / 1.0, _hunits[TUNIT_NS]) ),\
	    _humantime )

#endif /* __X_DURATION_H_ */
