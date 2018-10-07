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
 * @file   st.h
 * @date   Fri Sep 14 23:27:00 2018
 *
 * @brief  simple dynamic stack structure and function declarations
 *
 */

#ifndef ST_H_
#define ST_H_

#include <stdint.h>
#include <stdbool.h>

/* stack structure: data-based stack */
typedef struct {
    char *data; /* so we can do byte walks */
    size_t top;
    size_t capacity;
    size_t itemsize;
    size_t fill;
    unsigned int flags;
    bool empty;
} DStack;

/* stack structure: pointer-based stack */
typedef struct {
    void **data;
    size_t capacity;
    size_t fill;
    unsigned int flags;
    bool empty;
} PStack;

#define ST_NONE		0
#define ST_NO_SHRINK	1<<0
#define ST_NO_GROW	1<<1

/* allocate and initialise new stack */
DStack*		dstCreate(const size_t capacity, const size_t itemsize, const unsigned int flags);
PStack*		pstCreate(const size_t capacity, const unsigned int flags);
/* free FIFO stack */
void		dstFree(DStack *stack);
void		pstFree(PStack *stack);
/* test callback for benchmarks and whatnot */
bool stDummyCallback(void *item);
/* iterate over stack contents with a callback */
void		dstWalk(DStack *stack, bool (*callback)(void*));
void		pstWalk(PStack *stack, bool (*callback)(void*));
/* dump an uint32_t stack */
void		dstDumpU32(DStack *stack);
void		pstDumpU32(PStack *stack);
/* push item onto top of stack */
void*		dstPush(DStack *stack, void *item);
void*		pstPush(PStack *stack, void *item);
/* pop item off top of stack */
void*		dstPop(DStack *stack);
void*		pstPop(PStack *stack);

#endif /* ST_H_ */
