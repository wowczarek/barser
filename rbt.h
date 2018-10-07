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
 * @file   rbt.h
 * @date   Fri Sep 14 23:27:00 2018
 *
 * @brief  red-black tree type and function declarations
 *
 */

#ifndef RBT_H_
#define RBT_H_

#include <stdint.h>
#include <stdbool.h>

/* constants */

/* silent / chatty tree verification */
#define RB_CHATTY true
#define RB_QUIET false

/* full verification or stop on first violation */
#define RB_STOP true
#define RB_FULL false

/* ascending / descending (ltr / rtl) traversal order */
#define RB_ASC 0
#define RB_DESC 1

/* child positions so as to avoid using indexes directly */
#define RB_LEFT 0
#define RB_RIGHT 1

/* range traversal limits */
#define RB_INCL 0 /* include limit */
#define RB_EXCL 1 /* exclude limit */
#define RB_INF  2 /* no limit */

typedef struct RbNode RbNode;

/* the tree node */
struct RbNode {
    /* indexed children, makes life so much easier */
    RbNode* children[2];
    RbNode* parent;
    void* value;
    uint32_t key;
    /* could be something bigger with bit flags. To investigate: child and parent colour flags as well as our own */
    bool red;
};

/* tree container; node count is maintained at minimal cost */
typedef struct {
    RbNode *root;
    uint32_t count;
} RbTree;

/*
 * callback typedef. Callback must return the node it was passed (in case it frees it and returns NULL), and takes arguments:
 * tree, node, user data pointer, black height of node, height (path length) of node,
 * pointer to bool (if the callback sets this to false, traversal stops), node count so far
 */
typedef RbNode* (*RbCallback) (RbTree*, RbNode*, void*, const int, const int, bool*, const uint32_t);
/* use this if you want, but readability will suffer */
#define RB_CB_ARGS RbTree *tree, RbNode *node, void *user, const int bh, const int height, bool *cont, const uint32_t nodenumber

/* create an empty red-black tree */
RbTree*		rbCreate();

/* search for key, return node */
RbNode*		rbSearch(RbNode *root, const uint32_t key);

/* insert key into tree */
RbNode*		rbInsert(RbTree *tree, const uint32_t key);

/* delete node from tree (ideally one that *is* in the tree...) */
void		rbDeleteNode(RbTree *tree, RbNode *node);

/* delete node with given key from tree */
void		rbDeleteKey(RbTree *tree, const uint32_t key);

/* in-order traversal, dir = RB_ASC | RB_DESC, running specified callback function on each node */
void		rbInOrderTrack(RbTree *tree, RbCallback callback, void *user, const int dir);
/*
 * range traversal functions return the number of nodes within range, and take extra arguments:
 * range from, from qualifier (RB_INCL = inclusive, RB_EXCL = exclusive, RB_INF = all entries), range to, to qualifier
 * note: the order of ranges (from = low, to = high) is the same regardless of direction specified
*/
uint32_t	rbInOrderRangeTrack(RbTree *tree, RbCallback callback, void *user, const int dir,
			const uint32_t low, const int lowqual, const uint32_t high, const int highqual);

/* "fast" versions do away with black height / height calculation - but maintain same callback type */
void		rbInOrder(RbTree *tree, RbCallback callback, void *user, const int dir);
uint32_t	rbInOrderRange(RbTree *tree, RbCallback callback, void *user, const int dir,
			const uint32_t low, const int lowqual, const uint32_t high, const int highqual);

/* breadth first traversal (level by level), dir RB_ASC = left to right, RB_DESC = right to left. Same callback type. */
void		rbBreadthFirstTrack(RbTree *tree, RbCallback callback, void *user, const int dir);
/* breadth first without height tracking */
void		rbBreadthFirst(RbTree *tree, RbCallback callback, void *user, const int dir);

/* basic callback to print node information. bh = black height, height = node height / path length */
RbNode*		rbDumpCallback(RbTree *tree, RbNode *node, void *user, const int bh, const int height, bool *cont, const uint32_t nodenumber);

/* empty callback for traversal tests */
RbNode*		rbDummyCallback(RbTree *tree, RbNode *node, void *user, const int bh, const int height, bool *cont, const uint32_t nodenumber);

/* verify red-black tree invariants, optionally displaying status on stderr and verifying every node (internally this is an in-order traversal with a verify callback) */
bool		rbVerify(RbTree *tree, bool chatty, bool stop);

/* free tree nodes and tree */
void		rbFree(RbTree *tree);

/* just free nodes */
void		rbEmpty(RbTree *tree);

#endif /* RBT_H_ */
