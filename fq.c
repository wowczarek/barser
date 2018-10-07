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
 * @file   fq.c
 * @date   Fri Sep 14 23:27:00 2018
 *
 * @brief  simple dynamic FIFO queue implementation with automatic size management.
 *         the queue was implemented mainly for use in red-black tree breadth-first traversal,
 *         which ideally needs a FIFO queue.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "fq.h"

#define FQ_MIN_CAPACITY 16

/* create a data queue */
DFQueue* dfqCreate(const size_t capacity, const size_t itemsize, const unsigned int flags) {

    DFQueue *ret;

    if(itemsize == 0) {
	return NULL;
    }

    ret = calloc(1, sizeof(DFQueue));

    if(ret != NULL) {
	ret->itemsize = itemsize;
	ret->capacity = (capacity < FQ_MIN_CAPACITY) ? FQ_MIN_CAPACITY : capacity;
	ret->data = malloc(ret->capacity * itemsize);
	ret->flags = flags;
	ret->empty = true;
    }

    return ret;

}

/* create a pointer queue */
PFQueue* pfqCreate(const size_t capacity, const unsigned int flags) {

    PFQueue *ret;

    ret = calloc(1, sizeof(PFQueue));

    if(ret != NULL) {
	ret->capacity = (capacity < FQ_MIN_CAPACITY) ? FQ_MIN_CAPACITY : capacity;
	ret->data = malloc(ret->capacity * sizeof(void*));
	ret->flags = flags;
	ret->empty = true;
    }

    return ret;

}


/* free queue and data */
void dfqFree(DFQueue *queue) {

    if(queue != NULL) {
	if(queue->data != NULL) {
	    free(queue->data);
	}
	free(queue);
    }
}

void pfqFree(PFQueue *queue) {

    if(queue != NULL) {
	if(queue->data != NULL) {
	    free(queue->data);
	}
	free(queue);
    }
}


/* callback to dump uint32_t, for testing */
static bool fqU32Callback(void *item, const bool ishead, const bool istail) {

    uint32_t n = *(uint32_t*)item;
    char *mark = (ishead && istail) ? "ht" : ishead ? "h " : istail ? "t " : "  ";

    if(item == NULL) {
	printf("[%s NULL] ", mark);
    } else {
	printf("[%s %u] ", mark, n);
    }

    return true;
}

/* test callback for benchmarks and whatnot */
bool fqDummyCallback(void *item, const bool ishead, const bool istail) {
    return true;
}

/* walk the queue with a callback */
void dfqWalk(DFQueue * queue, bool (*callback)(void *item, const bool ishead, const bool istail)) {

    if(callback == NULL) {
	return;
    }

    if(queue->empty) {
	callback(NULL, true, true);
	return;
    }

    for(int i = queue->head; i != (queue->tail + 1); i++) {

	if(i == queue->capacity) {
	    i = 0;
	}

	if(!callback(queue->data + i * queue->itemsize  , i == queue->head, i == queue->tail)) {
	    return;
	}

    }

}

void pfqWalk(PFQueue *queue, bool (*callback)(void *item, const bool ishead, const bool istail)) {

    if(callback == NULL) {
	return;
    }

    if(queue->empty) {
	callback(NULL, true, true);
	return;
    }

    for(int i = queue->head; i != (queue->tail + 1); i++) {

	if(i == queue->capacity) {
	    i = 0;
	}

	if(!callback(queue->data[i], i == queue->head, i == queue->tail)) {
	    return;
	}

    }

}

/* dump an uint32_t queue */
void dfqDumpU32(DFQueue *queue) {

    dfqWalk(queue, fqU32Callback);
    printf("\n");

}
void pfqDumpU32(PFQueue *queue) {

    pfqWalk(queue, fqU32Callback);
    printf("\n");

}

/* add to queue tail, return item or NULL if cannot resize */
void* dfqPush(DFQueue *queue, void *item) {

    /* check if we need to grow */
    if(queue->fill == queue->capacity) {

	if(queue->flags & FQ_NO_GROW) {
	    return NULL;
	}

	/* grow */
	queue->capacity <<= 1;
	queue->data = realloc(queue->data, queue->capacity  * queue->itemsize);

	/* wrapped queue: move head-end towards the end */
	if(queue->tail < queue->head) {

	    size_t oldhead = queue->head;
	    queue->head += (queue->capacity >> 1);
	    memcpy(queue->data + queue->head * queue->itemsize, queue->data + oldhead * queue->itemsize, (queue->capacity - queue->head) * queue->itemsize);

	}

    }

    queue->fill++;

    /* so that we don't start from second spot */
    if(!queue->empty) {
	/* very little performance difference between this and a mask - and allows us to declare non-power of 2 queue sizes */
	queue->tail++;
	if(queue->tail == queue->capacity) {
	    queue->tail = 0;
	}
    }

    /* get in! */
    memcpy(queue->data + queue->tail * queue->itemsize, item, queue->itemsize);

    queue->empty = false;
    return item;

}

void* pfqPush(PFQueue *queue, void *item) {

    /* check if we need to grow */
    if(queue->fill == queue->capacity) {

	if(queue->flags & FQ_NO_GROW) {
	    return NULL;
	}

	/* grow */
	queue->capacity <<= 1;
	queue->data = realloc(queue->data, queue->capacity * sizeof(void*));

	/* wrapped queue: move head-end towards the end */
	if(queue->tail < queue->head) {

	    size_t oldhead = queue->head;
	    queue->head += (queue->capacity >> 1);
	    memcpy(queue->data + queue->head, queue->data + oldhead, (queue->capacity - queue->head) * sizeof(void*));

	}

    }

    queue->fill++;

    /* so that we don't start from second spot */
    if(!queue->empty) {
	/* very little performance difference between this and a mask - and allows us to declare non-power of 2 queue sizes */
	queue->tail++;
	if(queue->tail == queue->capacity) {
	    queue->tail = 0;
	}
    }
    /* get in! */
    queue->data[queue->tail] = item;

    queue->empty = false;
    return item;

}


/* grab from queue head */
void* dfqPop(DFQueue *queue) {

    void *ret;

    if(queue->fill > 0) {

	/* need to shrink */
	if(!(queue->flags & FQ_NO_SHRINK) && queue->fill < (queue->capacity >> 2) && queue->capacity > FQ_MIN_CAPACITY) {

	    /* tail, empties, head:  move (head..end) to end of next capacity */
	    if(queue->tail < queue->head) {

		size_t oldhead = queue->head;
		queue->head -= (queue->capacity >> 1);
		memmove(queue->data + queue->head * queue->itemsize, queue->data + oldhead * queue->itemsize, (queue->capacity - oldhead) * queue->itemsize);

	    /* (empties), head, tail: move (head..tail) to front */
	    } else if(queue->head > 0) {

	        memmove(queue->data, queue->data + queue->head * queue->itemsize, (queue->tail - queue->head + 1) * queue->itemsize);
	        queue->tail = queue->tail - queue->head;
	        queue->head = 0;

	    }
	    /* slurp */
	    queue->capacity >>= 1;
	    queue->data = realloc(queue->data, queue->capacity * queue->itemsize);
	}

	queue->fill--;

	ret = queue->data + queue->head * queue->itemsize;

	if(queue->fill == 0) {
	    queue->head = queue->tail = 0;
	    queue->empty = true;
	} else {
	    /* very little performance difference between this and a mask - and allows us to declare non-power of 2 queue sizes */
	    queue->head++;
	    if(queue->head == queue->capacity) {
		queue->head = 0;
	    }

	}

	return ret;

    }

    return NULL;
}

void* pfqPop(PFQueue *queue) {

    void *ret;

    if(queue->fill > 0) {

	/* need to shrink */
	if(!(queue->flags & FQ_NO_SHRINK) && queue->fill < (queue->capacity >> 2) && queue->capacity > FQ_MIN_CAPACITY) {

	    /* tail, empties, head:  move (head..end) to end of next capacity */
	    if(queue->tail < queue->head) {

		size_t oldhead = queue->head;
		queue->head -= (queue->capacity >> 1);
		memmove(queue->data + queue->head, queue->data + oldhead, (queue->capacity - oldhead) * sizeof(void*));

	    /* (empties), head, tail: move (head..tail) to front */
	    } else if(queue->head > 0) {

	        memmove(queue->data, queue->data + queue->head, (queue->tail - queue->head + 1) * sizeof(void*));
	        queue->tail = queue->tail - queue->head;
	        queue->head = 0;

	    }
	    /* slurp */
	    queue->capacity >>= 1;
	    queue->data = realloc(queue->data, queue->capacity * sizeof(void*));
	}

	queue->fill--;

	ret = queue->data[queue->head];

	if(queue->fill == 0) {
	    queue->head = queue->tail = 0;
	    queue->empty = true;
	} else {
	    /* very little performance difference between this and a mask - and allows us to declare non-power of 2 queue sizes */
	    queue->head++;
	    if(queue->head == queue->capacity) {
		queue->head = 0;
	    }
	}

	return ret;

    }

    return NULL;
}
