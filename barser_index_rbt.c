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
 * @brief  A red-black tree based index implementation for barser;
 *
 */


#include <stdint.h>

#include "rbt/rbt.h"
#include "barser.h"

/* min, max, everybody needs min/max */
#ifndef min
#define min(a,b) ((a < b) ? (a) : (b))
#endif

#ifndef max
#define max(a,b) ((a > b) ? (a) : (b))
#endif


/*
 * index management wrappers for rbt
 */

/* create index */
void* bsIndexCreate() {

    return rbCreate();

}

/* free index */
void bsIndexFree(void* index) {
    rbFree(index);
}

/* retrieve node from index */
void* bsIndexGet(void *index, const uint32_t hash) {

    RbNode *ret = rbSearch(((RbTree*)index)->root, hash);
    /* this will eventually return a linked list to handle collisions - we don't care */
    return (ret == NULL) ? ret : ret->value;
}

/* insert node into index */
void bsIndexPut(BsDict *dict, BsNode* node) {

    RbNode* inode = rbInsert((RbTree*)(dict->index), node->hash);

    if(inode->value != NULL) {
	BsNode *v = inode->value;
	BS_GETNP(node, p1);
	BS_GETNP(v, p2);
#ifdef COLL_DEBUG
#include <stdio.h>
	printf("'%s' and '%s' share hash 0x%08x\n", p1, p2, node->hash);
#endif
	dict->collcount++;
	v->collcount++;
	dict->maxcoll = max(dict->maxcoll, v->collcount);
    } else {
	inode->value = node;
    }

}

/* delete node from index */
void bsIndexDelete(void *index, const BsNode* node) {

    RbTree *tree = index;

    RbNode *n = rbSearch(tree->root, node->hash);

    if(n != NULL && n->value == node) {
	rbDeleteNode(tree, n);
    }

}
