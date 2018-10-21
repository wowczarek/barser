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
 * @file   linked_list.c
 * @date   Sat Jan 9 16:14:10 2015
 *
 * @brief  doubly linked list implemented as macros for inclusion in structures,
 *         and as a standalone structure.
 */

#include <stdlib.h>

#include "linked_list.h"

/* create a linked list member */
static inline LListMember* llCreateMember() {

    LListMember* out = malloc(sizeof(LListMember));

    if(out != NULL) {
	out->_prev = NULL;
	out->_next = NULL;
	out->_first = NULL;
	return out;
    }

    return NULL;

}

/* create a linked list */
LList* llCreate() {

    LList* out = malloc(sizeof(LList));

    if(out != NULL) {
	out->_firstChild = NULL;
	out->_lastChild = NULL;
	return out;
    }

    return NULL;

}

/* free linked list members only */
void llEmpty(LList* list) {

    LListMember *m = list->_firstChild;
    LListMember *tmp;

    while(m != NULL) {

	tmp = m;
	m = m->_next;
	free(tmp);
    }

}

/* free a linked list and all its members */
void llFree(LList* list) {

    if(list != NULL) {

	llEmpty(list);
	free(list);
    }

}

/* append item to list */
void llAppendItem(LList* list, void* item) {

    if(list != NULL) {

	LListMember *m = llCreateMember();
    
	if(m != NULL) {
	    m->value = item;
	    LL_APPEND_DYNAMIC(list, m);
	}

    }

}

/* remove item from list */
void llRemoveItem(LList* list, const void* item) {

    if(list != NULL) {

	LListMember* m = NULL;

	LL_FOREACH_DYNAMIC(list, m) {
	    if(m->value == item) {
		break;
	    }
	}

	if(m != NULL) {
	    LL_REMOVE_DYNAMIC(list, m);
	    free(m);
	}

    }

}

/* remove list member from list */
void llRemove(LList* list, LListMember *member) {

    if(list != NULL) {

	LListMember* m = NULL;

	LL_FOREACH_DYNAMIC(list, m) {
	    if (m == member) {
		break;
	    }
	}

	if(m != NULL) {
	    LL_REMOVE_DYNAMIC(list, m);

	}

    }


}

/* check if list contains member and return it ( magpie robin thrush sparrow warbler pipit COCK ) */
LListMember* llGetMember(LList* list, const LListMember * member) {

    if(list != NULL) {

	LListMember* m = NULL;

	LL_FOREACH_DYNAMIC(list, m) {
	    if (m == member) {
		return m;
	    }
	}

    }

    return NULL;

}

/* check if list contains item and return list member */
LListMember* llGetItemHolder(LList* list, const void *item) {

    if(list != NULL) {

	LListMember* m = NULL;

	LL_FOREACH_DYNAMIC(list, m) {
	    if (m->value == item) {
		return m;
	    }
	}

    }

    return NULL;

}

/* check if list is empty */
bool llisEmpty(const LList* list) {

    return(list->_firstChild == NULL);

}
