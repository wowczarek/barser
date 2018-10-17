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
 * @file   linked_list.h
 * @date   Sat Jan 9 16:14:10 2015
 *
 * @brief  doubly linked list implemented as macros for inclusion in structures,
 *         and as a standalone structure.
 */

#ifndef CCK_LINKEDLIST_H_
#define CCK_LINKEDLIST_H_

#include <stdint.h>
#include <stdbool.h>

/* static list parent / holder in a module */
#define LL_ROOT(vartype) \
    static vartype *_first = NULL; \
    static vartype *_last = NULL; \
    static uint32_t _serial = 0;

/* list parent / holder to be included in a structure */
#define LL_HOLDER(vartype) \
    vartype *_firstChild;\
    vartype *_lastChild;

/* list child / member to be included in a structure */
#define LL_MEMBER(vartype) \
    vartype **_first; \
    vartype *_next; \
    vartype *_prev;

/* append variable to statically embedded linked list */
#define LL_APPEND_STATIC(var) \
    if(_first == NULL) { \
	_first = var; \
    } \
    if(_last != NULL) { \
        var->_prev = _last; \
        var->_prev->_next = var; \
    } \
    _last = var; \
    var->_first = &_first; \
    var->_next = NULL; \
    var->_serial = _serial; \
    _serial++;

/* append variable to linked list held in the holder variable */
#define LL_APPEND_DYNAMIC(holder, var) \
    if(holder->_firstChild == NULL) { \
	holder->_firstChild = var; \
    } \
    if(holder->_lastChild != NULL) { \
        var->_prev = holder->_lastChild; \
        var->_prev->_next = var; \
    } \
    holder->_lastChild = var; \
    var->_next = NULL; \
    var->_first = &holder->_firstChild;

/* remove variable from a statically embedded list */
#define LL_REMOVE_STATIC(var) \
    if(var == _last) { \
        _serial = var->_serial; \
    } \
    if(var->_prev == NULL) { \
	_first = var->_next;\
    } else {\
	var->_prev->_next = var->_next; \
    }\
    if(var->_next == NULL) { \
	_last = var->_prev;\
    } else { \
	var->_next->_prev = var->_prev; \
    }\
    var->_next = NULL; \
    var->_prev = NULL; \
    var->_first = NULL;

/* remove variable from holder variable */
#define LL_REMOVE_DYNAMIC(holder, var) \
    if(var->_prev == NULL) { \
	holder->_firstChild = var->_next;\
    } else {\
	var->_prev->_next = var->_next; \
    }\
    if(var->_next == NULL) { \
	holder->_lastChild = var->_prev;\
    } else { \
	var->_next->_prev = var->_prev; \
    }\
    var->_next = NULL; \
    var->_prev = NULL; \
    var->_first = NULL;
/* simple check if list is empty */
#define LL_EMPTY_DYNAMIC(holder) ((holder)->firstChild == NULL\
				 && (holder)->_lastChild == NULL)

/* simple check if local list is empty */
#define LL_EMPTY_STATIC() (_first == NULL && _last == NULL)

/* foreach loop within a module, assigning var as we walk */
#define LL_FOREACH_STATIC(var) \
    for(var = _first; var != NULL; var = var->_next)

/* reverse linked list walk within module */
#define LL_FOREACH_STATIC_REVERSE(var) \
    for(var = _last; var != NULL; var = var->_prev)

/* foreach within holder struct */
#define LL_FOREACH_DYNAMIC(holder, var) \
    for(var = (holder)->_firstChild; var != NULL; var = var->_next)

/* reverse foreach within holder struct */
#define LL_FOREACH_DYNAMIC_REVERSE(holder, var) \
    for(var = (holder)->_lastChild; var != NULL; var = var->_prev)

#define LL_FOREACH_INNER(holder, var) \
    for(var = (*(holder))->_firstChild; var != NULL; var = var->_next)

/* walk list from last member downwards, calling fun to delete members */
#define LL_DESTROYALL(helper, fun) \
    while(_first != NULL) { \
        helper = _last; \
        fun(&helper); \
    }

/* pool holder data to be included in a structure */
#define POOL_HOLDER(vartype, count) \
    vartype _pooldata[count]; \
    vartype *_first; \
    vartype *_last; \
    struct { \
    vartype *_first; \
    vartype *_last; \
    } _pool; \

/* linked list member */
typedef struct LListMember LListMember;
struct LListMember {
    LL_MEMBER(LListMember);
    void* value;
};

/* linked list head */
typedef struct {
    LL_HOLDER(LListMember);
} LList;


/* basic linked list management code */

/* create a linked list */
LList* llCreate();
/* free a linked list and all its members */
void llFree(LList* list);
/* free linked list members only */
void llEmpty(LList* list);
/* append item to list */
void llAppendItem(LList* list, void* item);
/* remove item from list */
void llRemoveItem(LList* list, const void* item);
/* remove list member from list */
void llRemove(LList* list, LListMember *member);
/* check if list contains member and return it ( magpie robin thrush sparrow warbler pipit COCK ) */
LListMember* llGetMember(LList* list, const LListMember * member);
/* check if list contains item and return member */
LListMember* llGetItemHolder(LList* list, const void *item);
/* check if list is empty */
bool llisEmpty(const LList* list);

#endif /* CCK_LINKEDLIST_H_ */

