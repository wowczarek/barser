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
 * @file   st.c
 * @date   Fri Sep 14 23:27:00 2018
 *
 * @brief  simple dynamic stack implementation with automatic size management.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "st.h"

#define ST_MIN_CAPACITY 16

/* create a data stack */
DStack* dstCreate(const size_t capacity, const size_t itemsize, const unsigned int flags) {

    DStack *ret;

    if(itemsize == 0) {
	return NULL;
    }

    ret = calloc(1, sizeof(DStack));

    if(ret != NULL) {
	ret->itemsize = itemsize;
	ret->capacity = (capacity < ST_MIN_CAPACITY) ? ST_MIN_CAPACITY : capacity;
	ret->data = malloc(ret->capacity * itemsize);
	ret->flags = flags;
	ret->empty = true;
    }

    return ret;

}

/* create a pointer stack */
PStack *pstCreate(const size_t capacity, const unsigned int flags) {

    PStack *ret;

    ret = calloc(1, sizeof(PStack));

    if(ret != NULL) {
	ret->capacity = (capacity < ST_MIN_CAPACITY) ? ST_MIN_CAPACITY : capacity;
	ret->data = malloc(ret->capacity * sizeof(void*));
	ret->flags = flags;
	ret->empty = true;
    }

    return ret;

}


/* free stack and data */
void dstFree(DStack *stack) {

    if(stack != NULL) {
	if(stack->data != NULL) {
	    free(stack->data);
	}
	free(stack);
    }
}

void pstFree(PStack *stack) {

    if(stack != NULL) {
	if(stack->data != NULL) {
	    free(stack->data);
	}
	free(stack);
    }
}


/* callback to dump uint32_t, for testing */
static bool stU32Callback(void *item) {

    uint32_t n = *(uint32_t*)item;

    if(item == NULL) {
	printf("[NULL] ");
    } else {
	printf("[%u] ", n);
    }

    return true;
}

/* test callback for benchmarks and whatnot */
bool stDummyCallback(void *item) {
    return true;
}

/* walk the stack with a callback */
void dstWalk(DStack *stack, bool (*callback)(void *item)) {

    if(callback == NULL) {
	return;
    }

    if(stack->empty) {
	callback(NULL);
	return;
    }

    for(int i = 0; i < stack->fill; i++) {

	if(!callback(stack->data + i * stack->itemsize)) {
	    return;
	}

    }

}

void pstWalk(PStack *stack, bool (*callback)(void *item)) {

    if(callback == NULL) {
	return;
    }

    if(stack->empty) {
	callback(NULL);
	return;
    }

    for(int i = 0; i < stack->fill; i++) {

	if(!callback(stack->data[i])) {
	    return;
	}

    }

}

/* dump an uint32_t stack */
void dstDumpU32(DStack *stack) {

    dstWalk(stack, stU32Callback);
    printf("\n");

}
void pstDumpU32(PStack *stack) {

    pstWalk(stack, stU32Callback);
    printf("\n");

}

/* add to stack tail, return item or NULL if cannot resize */
void* dstPush(DStack *stack, void *item) {

    /* check if we need to grow */
    if(stack->fill == stack->capacity) {

	if(stack->flags & ST_NO_GROW) {
	    return NULL;
	}
	/* grow */
	stack->data = realloc(stack->data, (stack->capacity  <<= 1) * stack->itemsize);
    }

    /* get in! */
    memcpy(stack->data + stack->fill++ * stack->itemsize, item, stack->itemsize);
    stack->empty = false;

    return item;

}

void* pstPush(PStack *stack, void *item) {

    /* check if we need to grow */
    if(stack->fill == stack->capacity) {
	if(stack->flags & ST_NO_GROW) {
	    return NULL;
	}

	/* grow */
	stack->data = realloc(stack->data, (stack->capacity  <<= 1) * sizeof(void*));

    }

    /* get in! */
    stack->data[stack->fill++] = item;
    stack->empty = false;

    return item;

}


/* grab from stack head */
void* dstPop(DStack *stack) {

    void *ret;

    if(stack->fill > 0) {

	/* need to shrink */
	if(!(stack->flags & ST_NO_SHRINK) && stack->fill < (stack->capacity >> 2)  && stack->capacity > ST_MIN_CAPACITY) {
	    stack->data = realloc(stack->data, (stack->capacity >>= 1) * stack->itemsize);
	}

	ret = stack->data + --stack->fill * stack->itemsize;

	if(stack->fill == 0) {
	    stack->empty = true;
	}

	return ret;

    }

    return NULL;
}

void* pstPop(PStack *stack) {

    void *ret;

    if(stack->fill > 0) {

	/* need to shrink */
	if(!(stack->flags & ST_NO_SHRINK) && stack->fill < (stack->capacity >> 2) && stack->capacity > ST_MIN_CAPACITY) {
	    stack->data = realloc(stack->data, (stack->capacity >>= 1) * sizeof(void*));
	}

	ret = stack->data[--stack->fill];

	if(stack->fill == 0) {
	    stack->empty = true;
	}

	return ret;

    }

    return NULL;

}
