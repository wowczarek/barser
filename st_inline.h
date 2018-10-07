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
 * @file   st_inline.h
 * @date   Fri Sep 14 23:27:00 2018
 *
 * @brief  simple local inline stack implemented as macros only
 *
 */

#ifndef ST_INLINE_H_
#define ST_INLINE_H_

#include <stdbool.h>

/* ======== pointer stack ======== */

/* declare a stack of given name, element pointer type and minimum / initial capacity */
#define PST_DECL(name, type, minsize) type *name##_st;\
				size_t name##_sh = 0;\
				size_t name##_ss = minsize;\
				size_t name##_ms = minsize;\
				size_t name##_es = sizeof(type);

/* initialise a stack */
#define PST_INIT(name) name##_st = malloc(name##_ms * sizeof(void*));

/* blindly push data onto top of stack - use only in a loop that checks if stack is full */
#define PST_PUSH(name, item) name##_st[name##_sh++] = item;

/* safely push data onto top of stack, grow stack if required */
#define PST_PUSH_GROW(name, item) if(name##_sh >= name##_ss) {\
				name##_st = realloc(name##_st, (name##_ss <<= 1) * name##_es);\
			    }\
			    name##_st[name##_sh++] = item;

/* stack checks */
#define PST_EMPTY(name) (name##_sh <= 0)
#define PST_NONEMPTY(name) (name##_sh > 0)
#define PST_FULL(name) (name##_sh == name##_ss)
#define PST_NONFULL(name) (name##_sh < name##_ss)

/* blindly pop data off top of stack - use only in a loop that checks if stack is empty */
#define PST_POP(name)		name##_st[--name##_sh]

/* safely pop data off top of stack, return NULL if empty */
#define PST_POP_SAFE(name)	PST_NONEMPTY(name) ? PST_POP(name) : NULL

/* pop data off top of stack and shrink it if need be (shrink by half when 25% capacity reached) */
#define PST_POP_SHRINK(name) (\
		name##_st = (name##_ss > name##_ms && name##_sh < (name##_ss >> 2)) ? realloc(name##_st, (name##_ss >> 1) * name##_es) : name##_st,\
		name##_st[--name##_sh])

/* safely pop data off top of stack, shrink if need be and return NULL if empty */
#define PST_POP_SHRINK_SAFE(name)	PST_NONEMPTY(name) ? PST_POP_SHRINK(name) : NULL

/* blindly peek at the top element of stack */
#define PST_PEEK(name) name##_st[name##_sh - 1]

/* safely peek at the top element of stack and return NULL if empty */
#define PST_PEEK_SAFE(name) PST_NONEMPTY(name) ? PST_PEEK(name) : NULL

/* free stack data allocation */
#define PST_FREE(name) free(name##_st);

/* free every pointer on the stack and reset the stack */
#define PST_FREEDATA(name) for(int name##_i = 0; name##_i < name##_sh; name##_i++) { free(name##_st[name##_i]); }; name##_sh = 0;

/* ======== data stack ======== */

/* declare a stack of given name, element type and minimum / initial capacity */
#define DST_DECL(name, type, minsize) type *name##_st;\
				type *name##_sp;\
				size_t name##_sh = 0;\
				size_t name##_ss = minsize;\
				size_t name##_ms = minsize;\
				bool name##_stest = false;\
				size_t name##_es = sizeof(type);

/* initialise a stack */
#define DST_INIT(name) name##_st = malloc(name##_ms * name##_es);\
			    name##_sp = name##_st;

/* blindly push data onto top of stack - use only in a loop that checks if stack is full */
#define DST_PUSH(name, item) name##_sh++;\
			    *(name##_sp++) = item;

/* safely push data onto top of stack, grow stack if required */
#define DST_PUSH_GROW(name, item) name##_sh++;\
			    if(name##_sh >= name##_ss) {\
				name##_st = realloc(name##_st, (name##_ss <<= 1) * name##_es);\
				name##_sp = name##_st + name##_sh - 1;\
			    }\
			    *(name##_sp++) = item;

/* stack checks */
#define DST_EMPTY(name) (name##_sp -= name##_st)
#define DST_NONEMPTY(name) (name##_sp != name##_st)
#define DST_FULL(name) (name##_sh == name##_ss)
#define DST_NONFULL(name) (name##_sh < name##_ss)

/* blindly pop data off top of stack - use only in a loop that checks if stack is empty */
#define DST_POP(name)		(name##_sh--, --name##_sp)

/* safely pop data off top of stack, return NULL if empty */
#define DST_POP_SAFE(name)	PST_NONEMPTY(name) ? (name##_sh--, --name##_sp) : NULL

/* pop data off top of stack and shrink it if need be (shrink by half when 25% capacity reached) */
#define DST_POP_SHRINK(name) (\
		name##_stest = (name##_ss > name##_ms && name##_sh < (name##_ss >> 2)),\
		name##_st = name##_stest ? realloc(name##_st, (name##_ss >> 1) * name##_es) : name##_st,\
		name##_sp = name##_stest ? (name##_ss >>= 1, name##_st + name##_sh) : name##_sp,\
		name##_sh--,\
		--name##_sp)

/* safely pop data off top of stack, shrink if need be and return NULL if empty */
#define DST_POP_SHRINK_SAFE(name) PST_NONEMPTY(name) ? PST_POP_SHRINK(name) : NULL

/* blindly peek at the top element of stack */
#define DST_PEEK(name) (name##_sp - 1)

/* safely peek at the top element of stack and return NULL if empty */
#define DST_PEEK_SAFE(name) PST_NONEMPTY(name) ? (name##_sp - 1) : NULL

/* free stack data allocation */
#define DST_FREE(name) free(name##_st);

#endif /* ST_INLINE_H_ */
