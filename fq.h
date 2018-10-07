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
 * @file   fq.h
 * @date   Fri Sep 14 23:27:00 2018
 *
 * @brief  simple dynamic FIFO queue structure and function declarations
 *
 */

#ifndef FQ_H_
#define FQ_H_

#include <stdint.h>
#include <stdbool.h>

/* queue structure: data-based queue */
typedef struct {
    char *data; /* so we can do byte walks */
    size_t head;
    size_t tail;
    size_t capacity;
    size_t itemsize;
    size_t fill;
    unsigned int flags;
    bool empty;
} DFQueue;

/* queue structure: pointer-based queue */
typedef struct {
    void **data;
    size_t head;
    size_t tail;
    size_t capacity;
    size_t fill;
    unsigned int flags;
    bool empty;
} PFQueue;

#define FQ_NONE		0
#define FQ_NO_SHRINK	1<<0
#define FQ_NO_GROW	1<<1

/* allocate and initialise new FIFO queue*/
DFQueue*	dfqCreate(const size_t capacity, const size_t itemsize, const unsigned int flags);
PFQueue*	pfqCreate(const size_t capacity, const unsigned int flags);
/* free FIFO queue */
void		dfqFree(DFQueue *queue);
void		pfqFree(PFQueue *queue);
/* test callback for benchmarks and whatnot */
bool fqDummyCallback(void *item, const bool ishead, const bool istail);
/* iterate over queue contents with a callback */
void		dfqWalk(DFQueue *queue, bool (*callback)(void*, const bool, const bool));
void		pfqWalk(PFQueue *queue, bool (*callback)(void*, const bool, const bool));
/* dump an uint32_t queue */
void		dfqDumpU32(DFQueue *queue);
void		pfqDumpU32(PFQueue *queue);
/* push item onto tail of queue */
void*		dfqPush(DFQueue *queue, void *item);
void*		pfqPush(PFQueue *queue, void *item);
/* pop item off the head of queue */
void*		dfqPop(DFQueue *queue);
void*		pfqPop(PFQueue *queue);

#endif /* FQ_H_ */
