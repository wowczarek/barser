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
 * @file   rbt_index.c
 * @date   Sun Apr15 13:40:12 2018
 *
 * @brief  A red-black tree based index implementation for barser
 *
 */


#include <stdint.h>

#include "rbt/rbt.h"
#include "linked_list.h"
#include "barser.h"

/*
 * index management wrappers for rbt
 */

/* create index */
void* bsIndexCreate() {

    return rbCreatePrealloc(sizeof(LList), NULL);
}

/* free index */
void bsIndexFree(void* index) {

    rbFree(index);

}

/* retrieve node list from index */
LList* bsIndexGet(void *index, const uint32_t hash) {

    RbNode *ret = rbSearch(((RbTree*)index)->root, hash);

    if(ret != NULL) {
	return ret->value;
    }

    return NULL;

}

/* insert node into index */
void bsIndexPut(BsDict *dict, BsNode* node) {

    RbNode* inode = rbInsert((RbTree*)(dict->index), node->hash);

    if(inode != NULL && inode->value != NULL) {

	LList *l = inode->value;

	if(!llisEmpty(l)) {
	    BsNode* n = l->_firstChild->value;
	    BS_GETNP(n, p1);
	    BS_GETNP(node, p2);
	    #ifdef COLL_DEBUG
	    #include <stdio.h>
	    printf("'%s' and '%s' share hash 0x%08x\n", p1, p2, node->hash);
	    #endif
	    dict->collcount++;
	    n->collcount++;
	    dict->maxcoll = max(dict->maxcoll, n->collcount);

	}

	llAppendItem(l, node);

    } 

}

/* delete node from index */
void bsIndexDelete(void *index, BsNode* node) {

    RbTree *tree = index;

    RbNode *n = rbSearch(tree->root, node->hash);

    if(n != NULL) {

	llRemoveItem(n->value, node);
	
    }

}