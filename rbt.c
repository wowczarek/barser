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
 * @file   rbt.c
 * @date   Fri Sep 14 23:27:00 2018
 *
 * @brief  a simple non-thread safe red-black tree implementation with traversal and verification.
 *         all core functions are iterative (non-recursive), but do use stacks and queues.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "rbt.h"
#include "fq.h"
#include "st_inline.h"

/* helper macros */
#define rbRed(var) (var != NULL && var->red)
#define rbBlack(var) (var == NULL || !var->red)
#define rbDir(var) (var == var->parent->children[RB_RIGHT])
#define rbCname(var) (rbBlack(var) ? "black" : "red")

/* red-black tree verification state container */
typedef struct {
    int maxbh;
    int maxheight;
    bool valid;
    bool chatty;
    bool stop;
} RbVerifyState;

/* helper structure to assist with height / black height tracking during traversal */
typedef struct {
    int height;
    int bh;
    RbNode* node;
} RbNodeInfo;

/* it is what it is */
static inline RbNode* rbCreateNode(RbNode *parent, uint32_t key) {

    RbNode *ret = calloc(1, sizeof(RbNode));
    ret->key = key;
    ret->parent = parent;
    return ret;
}

/* replace one node with another (pointer replacement, not key/value swap) */
#if 0 /* unused for now */
static inline void rbReplaceNode(RbTree *tree, RbNode *a, RbNode *b) {

    memcpy(b, a, sizeof(RbNode));

    if(a->parent == NULL) {
	tree->root = b;
    } else {
	b->parent->children[rbDir(a)] = b;
    }

    if(b->children[RB_LEFT] != NULL) {
	b->children[RB_LEFT]->parent = b;
    }

    if(b->children[RB_RIGHT] != NULL) {
	b->children[RB_RIGHT]->parent = b;
    }

}
#endif

/* binary search tree insertion, return newly added node - or existing node if found */
static inline RbNode* bstInsert(RbTree *tree, const uint32_t key) {

    RbNode* current = tree->root;
    RbNode* parent = NULL;
    int dir = 0;

    /* find the parent to attach new node, return if already exists */
    while(current != NULL) {

	if(current->key == key) {
	    return current;
	}

	parent = current;
	dir = ( key > current->key );
	current = current->children[dir];

    }

    /* create a new node, mark it red */
    current = rbCreateNode(parent, key);
    current->red = true;
    tree->count++;

    /* link parent with new node */
    if(parent != NULL) {
	parent->children[dir] = current;
    }

    return current;

}

/* perform a rotation of the given node in given direction */
static inline void rbRotate(RbTree *tree, RbNode *root, const int dir) {

    /* pivot node */
    RbNode *pivot = root->children[!dir];

    /* swapsies */
    root->children[!dir] = pivot->children[dir];
    if(pivot->children[dir] != NULL) {
	pivot->children[dir]->parent = root;
    }
    pivot->children[dir] = root;
    pivot->parent = root->parent;
    root->parent = pivot;

    /* link parent to pivot, or update tree root if pivot is the new root */
    if(pivot->parent == NULL) {
	tree->root = pivot;
    } else {
	int pdir = (pivot->parent->children[RB_RIGHT] == root);
	pivot->parent->children[pdir] = pivot;
    }

}

/* callback freeing a node */
static RbNode* rbFreeCallback(RbTree *tree, RbNode *node, void *user, const int bh, const int height, bool *cont, const uint32_t nodenumber) {

    if(node == tree->root) {
	tree->root = NULL;
    }

    free(node);

    return NULL;
}

/* callback used for tree verification */
static RbNode* rbVerifyCallback(RbTree *tree, RbNode *node, void *user, const int bh, const int height, bool *cont, const uint32_t nodenumber) {

    RbVerifyState *state = user;

    if(node != NULL) {

	if(height > state->maxheight) {
	    state->maxheight = height;
	}

	/* check black height of node with zero or one child */
	if(node->children[RB_LEFT] == NULL || node->children[RB_RIGHT] == NULL) {

	    if(state->maxbh == 0) {
		state->maxbh = bh;
	    }

	    /* if we see any changes in black height, tree is invalid */
	    if(bh != state->maxbh) {
		state->valid = false;
		if(state->chatty) {
		    fprintf(stderr, "Black height violation: key %d black height %d != previous black height seen %d\n", node->key, bh, state->maxbh);
		}
		if(state->stop) {
		    *cont = false;
		    return node;
		}
		state->maxbh = bh;
	    }

	}

	if(node->red && rbRed(node->parent)) {
	    state->valid = false;
	    if(state->chatty) {
		fprintf(stderr, "Red-red violation: key %d red -> parent key %d red\n", node->key, node->parent->key);
	    }
	    if(state->stop) {
		    *cont = false;
	    }
	}

    }

    return node;

}

/* create a red-black tree */
RbTree* rbCreate() {

    RbTree* ret = calloc(1, sizeof(RbTree));
    return ret;

}

/* binary search tree search (in a red-black tree) */
RbNode* rbSearch(RbNode *root, const uint32_t key) {

    RbNode* current = root;

    while(current != NULL) {

	if(current->key == key) {
	    return current;
	}

	current = current->children[key > current->key];

    }

    return NULL;
}

/* insert a key into the tree, return the newly inserted node, or existing node if key exists */
RbNode* rbInsert(RbTree *tree, const uint32_t key) {

    /* the new node is coloured red only on creation - if exists, no change of colour, so no violations */
    RbNode *ret = bstInsert(tree, key);
    RbNode *current = ret;

    /* empty tree, new root */
    if(tree->root == NULL) {
	tree->root = ret;
	tree->root->red = false;
	return ret;
    }

    /* travel upwards and correct red->red violations */
    while(rbRed(current) && rbRed(current->parent)) {

	RbNode *parent = current->parent;
	RbNode *grandparent = parent->parent;

	/* parent's direction */
	int dir = rbDir(parent);
	int otherdir = !dir;

	/* knowing the parent's direction, uncle is the other guy */
	RbNode *uncle = grandparent->children[otherdir];

	/* red uncle: recolour, move up */
	if(rbRed(uncle)) {
	    grandparent->red = true;
	    parent->red = false;
	    uncle->red = false;
	    current = grandparent;
	/* black uncle - rotate and recolour */
	} else {
	    /* current is in the opposite direction to parent - there will be two rotations */
	    if(current == parent->children[otherdir]) {
		    rbRotate(tree, parent, dir);
		    current = parent;
		    parent = current->parent;
	    }

	    /* rotate */
	    rbRotate(tree, grandparent, otherdir);

	    /* recolour, move up */
	    parent->red = false;
	    grandparent->red = true;
	    current = parent;

	}

    }

    tree->root->red = false;

    /* return new node */
    return ret;

}

/* binary search tree deletion with red-black tree fixup combined */
void rbDeleteNode(RbTree *tree, RbNode *node) {

    /* unbalanced parent, happy children, yay! */
    RbNode *ubparent = NULL;
    int dir = 0;

    if(node != NULL) {

	/* if the node to be deleted is has two children, we find the successor and work with it, since this is the node to delete */
	if(node->children[RB_LEFT] != NULL && node->children[RB_RIGHT] != NULL) {

	    /* right first */
	    RbNode *successor = node->children[RB_RIGHT];
	    while(successor->children[RB_LEFT] != NULL) {
		/* then left all the way */
		successor = successor->children[RB_LEFT];
	    }

	    /* copy the successor's data into old node, preserve colour */
	    node->key = successor->key;
	    node->value = successor->value;

	    /* need to delete this guy now */
	    node = successor;
	
	}
	
	/* at this point the node we are working with can ony have one child or zero children */

	/* if node has one child, this leads us to it. if it has none, it will point to NULL */
	RbNode *promoted = node->children[node->children[RB_LEFT] == NULL];

	/* fix parent link - or root link */
	if(node->parent == NULL) {
	    tree->root = promoted;
	} else {
	    dir = rbDir(node);
	    node->parent->children[dir] = promoted;
	}

	/* fix promoted node's parent link */
	if(promoted != NULL) {
	    promoted->parent = node->parent;
	}
	
	/* if node and node's child differ in colour, promoted node needs to be black to keep the black height, and we are done */
	if(node->red != rbRed(promoted)) {
	    if(!node->red) {
		promoted->red = false;
	    }
	    tree->count--;
	    free(node);
	    return;
	} else {
	    /* our disturbed node is removed, and instead of "double black" or other such nonsense, we track its parent and direction towards it */
	    ubparent = node->parent;
	    tree->count--;
	    free(node);
	    if(ubparent != NULL) {
		ubparent->children[dir] = NULL;
	    }
	}

	/* keep going as long as we have some rebalancing to do */
	while(ubparent != NULL ) {

	    int otherdir = !dir;
	    RbNode *ubsibling = ubparent->children[otherdir];

	    /* case 1: parent black, sibling red... the tree was balanced before, so if sibling red, parent must be black, recolour and continue */
	    if(rbRed(ubsibling)) {

		rbRotate(tree, ubparent, dir);
		ubparent->red = true;
		ubsibling->red = false;

	    /* case 2: sibling black (because not red, above), has red child on opposite side to unbalanced node: rotate, recolour, done */
	    } else if(rbRed(ubsibling->children[otherdir])) {

		ubsibling->children[otherdir]->red = false;
		ubsibling->red = ubparent->red;
		ubparent->red = false;
		rbRotate(tree, ubparent, dir);
		return;

	    /* case 3: sibling black, has red child on same side as deleted node: recolour, rotate and we turn into case 1 */
	    } else if(rbRed(ubsibling->children[dir])) {

		ubsibling->children[dir]->red = false;
		ubsibling->red = true;
		rbRotate(tree, ubsibling, otherdir);

	    /* case 4: red parent: recolour, done */
	    } else if(ubparent->red) {

		ubparent->red = false;
		ubsibling->red = true;
		return;

	    /* case 5: parent and sibling black - mark sibling red and rebalance from parent */
	    } else {

		ubsibling->red = true;
		if(ubparent->parent != NULL) {
		    /* no need to do this every time */
		    dir = rbDir(ubparent);
		}
		ubparent = ubparent->parent;

	    }
	
	}

    }

}

/* delete the node with the given key from red-black tree */
void rbDeleteKey(RbTree *tree, const uint32_t key) {

    rbDeleteNode(tree, rbSearch(tree->root, key));

}

/* in-order tree traversal with depth and black height tracking, with a callback to call on each node */
void rbInOrderTrack(RbTree *tree, RbCallback callback, void *user, const int dir) {

    RbNode *current, *tmp, *last = NULL;

    uint32_t nodenumber = 0;
    int bh = 0, height = 0;
    int otherdir = !dir;
    int lastdir = otherdir;
    bool cont = true;
    PST_DECL(stack, RbNode*, 16);

    current = tree->root;

    if(current != NULL) {

	PST_INIT(stack);

	while ( cont && (PST_NONEMPTY(stack) || current != NULL) ) {

	    if(current != NULL) {
		/* push */
		PST_PUSH_GROW(stack, current);

		/* maintain running height and black height */
		height++;
		bh += !current->red;
		if( lastdir == otherdir ) {
		    height++;
		}
		last = current;
		lastdir = dir;

		current = current->children[dir];
	    } else {
		/* pop */
		current = PST_POP(stack);

		/* maintain running height and black height */
		if(current == last && lastdir == dir) {
		    /* bounceback */
		    height--;
		} else {
		    for(tmp = last; tmp != NULL && tmp != current; tmp = tmp->parent) {
			height--;
			bh -= !tmp->red;
		    }
		}
		lastdir = otherdir;

		/* preserve the pointer first: this allows the callback to free the node if it wants that */
		tmp = current->children[otherdir];
		/* the callback is expected to return the node and return NULL if it frees it */
		if(callback == NULL) {
		    last = current;
		} else {
		    last = callback(tree, current, user, bh, height, &cont, nodenumber++);
		}
		current = tmp;

	    }

	}

	PST_FREE(stack);

    }

}

/* in-order tree traversal without depth and black height tracking, with a callback to call on each node */
void rbInOrder(RbTree *tree, RbCallback callback, void *user, const int dir) {

    RbNode *current, *tmp;

    uint32_t nodenumber = 0;
    int otherdir = !dir;
    bool cont = true;
    PST_DECL(stack, RbNode*, 16);

    current = tree->root;

    if(current != NULL) {

	PST_INIT(stack);

	while ( cont && (PST_NONEMPTY(stack) || current != NULL) ) {

	    if(current != NULL) {
		/* push */
		PST_PUSH_GROW(stack, current);
		current = current->children[dir];
	    } else {
		/* pop */
		current = PST_POP(stack);
		/* preserve the pointer first: this allows the callback to free the node if it wants that */
		tmp = current->children[otherdir];
		if(callback != NULL) {
		    callback(tree, current, user, 0, 0, &cont, nodenumber++);
		}
		current = tmp;
	    }

	}

	PST_FREE(stack);

    }

}

/* in-order traversal over a specified range, returns count of nodes in range */
uint32_t rbInOrderRange(RbTree *tree, RbCallback callback, void *user, const int dir,
		const uint32_t low, const int lowqual, const uint32_t high, const int highqual) {

    RbNode *current, *tmp;

    uint32_t nodenumber = 0;
    int otherdir = !dir;
    bool cont = true;
    uint32_t startrange = low;
    uint32_t endrange = high;
    PST_DECL(stack, RbNode*, 16);

    PST_INIT(stack);

    /* first we deal with inclusive / exclusive ranges */

    /* to-end ranges */
    if(lowqual == RB_INF) startrange = 0;
    if(highqual == RB_INF) endrange = ~0;
    /* establish the range values */
    if(highqual == RB_EXCL) endrange--;
    if(lowqual == RB_EXCL) startrange++;

    if(dir == RB_DESC) {
	/* swap since we are going in the other direction */
	uint32_t tmprange = startrange;
	startrange = endrange;
	endrange = tmprange;
    }

    current = tree->root;

    /* then we set up the stack for in-order traversal: only needs to contain the root and nodes where we turned in [dir] direction towards start range */

    while(current != NULL) {

	int tmpdir = (startrange > current->key);

	if(tmpdir == dir || current->key == startrange) {

	    PST_PUSH_GROW(stack, current);

	    if(current->key == startrange) {
		current = NULL;
		break;
	    }
	}

	current = current->children[tmpdir];

    }

    /* the rest is a regular in-order traversal, just with a break clause */

    if(tree->root != NULL) {

	while ( cont && (PST_NONEMPTY(stack) || current != NULL) ) {

	    if(current != NULL) {
		/* push */
		PST_PUSH_GROW(stack, current);
		current = current->children[dir];
	    } else {
		/* pop */
		current = PST_POP(stack);

		/* dir left and key less than, or dir right and key greater than, processing ends */
		if(dir && (current->key < endrange)) {
		    break;
		}
		if(otherdir && (current->key > endrange)) {
		    break;
		}

		/* preserve the pointer first: this allows the callback to free the node if it wants that */
		tmp = current->children[otherdir];
		if(callback == NULL) {
		    nodenumber++;
		} else {
		    callback(tree, current, user, 0, 0, &cont, nodenumber++);
		}

		current = tmp;
	    }

	}

	PST_FREE(stack);

    }

    return nodenumber;

}

/* in-order traversal over a specified range (version with height / black height tracking), returns count of nodes in range */
uint32_t rbInOrderRangeTrack(RbTree *tree, RbCallback callback, void *user, const int dir,
		const uint32_t low, const int lowqual, const uint32_t high, const int highqual) {

    RbNode *current, *tmp, *last = NULL;

    uint32_t nodenumber = 0;
    int bh = 0, height = 0;
    int otherdir = !dir;
    int lastdir = otherdir;
    bool cont = true;
    uint32_t startrange = low;
    uint32_t endrange = high;
    PST_DECL(stack, RbNode*, 16);

    PST_INIT(stack);

    /* first we deal with inclusive / exclusive ranges */

    /* to-end ranges */
    if(lowqual == RB_INF) startrange = 0;
    if(highqual == RB_INF) endrange = ~0;
    /* establish the range values */
    if(highqual == RB_EXCL) endrange--;
    if(lowqual == RB_EXCL) startrange++;

    if(dir == RB_DESC) {
	/* swap since we are going in the other direction */
	uint32_t tmprange = startrange;
	startrange = endrange;
	endrange = tmprange;
    }

    current = tree->root;

    /* then we set up the stack for in-order traversal: only needs to contain the root and nodes where we turned in [dir] direction towards start range */

    while(current != NULL) {

	int tmpdir = (startrange > current->key);

	bh += !current->red;
	height++;
	last = current;

	if(tmpdir == dir || current->key == startrange) {

	    PST_PUSH_GROW(stack, current);

	    if(current->key == startrange) {
		current = NULL;
		break;
	    }
	}

	current = current->children[tmpdir];

    }

    /* the rest is a regular in-order traversal, just with a break clause */

    if(tree->root != NULL) {

	while ( cont && (PST_NONEMPTY(stack) || current != NULL) ) {

	    if(current != NULL) {
		/* push */
		PST_PUSH_GROW(stack, current);

		/* maintain running height and black height */
		height++;
		bh += !current->red;
		if( lastdir == otherdir ) {
		    height++;
		}
		last = current;
		lastdir = dir;

		current = current->children[dir];
	    } else {
		/* pop */
		current = PST_POP(stack);

		/* maintain running height and black height */
		if(current == last && lastdir == dir) {
		    /* bounceback */
		    height--;
		} else {
		    for(tmp = last; tmp != NULL && tmp != current; tmp = tmp->parent) {
			height--;
			bh -= !tmp->red;
		    }
		}
		lastdir = otherdir;

		/* dir left and key less than, or dir right and key greater than, processing ends */
		if(dir && (current->key < endrange)) {
		    break;
		}
		if(otherdir && (current->key > endrange)) {
		    break;
		}

		/* preserve the pointer first: this allows the callback to free the node if it wants that */
		tmp = current->children[otherdir];
		/* the callback is expected to return the node and return NULL if it frees it */
		if(callback == NULL) {
		    last = current;
		    nodenumber++;
		} else {
		    last = callback(tree, current, user, bh, height, &cont, nodenumber++);
		}

		current = tmp;
	    }

	}

	PST_FREE(stack);

    }

    return nodenumber;

}


/* breadth-first tree traversal with height and black height tracking */
void rbBreadthFirstTrack(RbTree *tree, RbCallback callback, void *user, const int dir) {

    int otherdir = !dir;
    uint32_t nodenumber = 0;
    bool cont = true;
    DFQueue *queue = NULL;
    RbNodeInfo current = {1, 1, tree->root};
    RbNodeInfo tmp = current;
    RbNode *walker = tree->root;
    int bh = 0;

    /* find black height to get a good approximation of queue size needed */
    while(walker != NULL) {
	bh += !walker->red;
	walker = walker->children[RB_LEFT];
    }

    queue = dfqCreate(2 << (bh + 1), sizeof(RbNodeInfo), FQ_NO_SHRINK);

    if(current.node != NULL) {

	dfqPush(queue, &current);

	while(cont && !queue->empty) {

	    current = *(RbNodeInfo*)dfqPop(queue);

	    if((tmp.node = current.node->children[dir]) != NULL) {
		tmp.height = current.height + 1;
		tmp.bh = current.bh + !tmp.node->red;
		dfqPush(queue, &tmp);
	    }

	    if((tmp.node = current.node->children[otherdir]) != NULL) {
		tmp.height = current.height + 1;
		tmp.bh = current.bh + !tmp.node->red;
		dfqPush(queue, &tmp);
	    }

	    callback(tree, current.node, user, current.bh, current.height, &cont, nodenumber++);

	}

    }

    dfqFree(queue);

}

/* breadth-first tree traversal without height and black height tracking */
void rbBreadthFirst(RbTree *tree, RbCallback callback, void *user, const int dir) {

    int otherdir = !dir;
    uint32_t nodenumber = 0;
    bool cont = true;
    PFQueue *queue = NULL;
    RbNode *current = tree->root;
    int bh = 0;

    /* find black height to get a good approximation of queue size needed */
    while(current != NULL) {
	bh += !current->red;
	current = current->children[RB_LEFT];
    }

    queue = pfqCreate(2 << ( bh + 1 ), FQ_NO_SHRINK);

    current = tree->root;

    if(current != NULL) {

	pfqPush(queue, current);

	while(cont && !queue->empty) {

	    current = pfqPop(queue);

	    if(current->children[dir] != NULL) {
		pfqPush(queue, current->children[dir]);
	    }

	    if(current->children[otherdir] != NULL) {
		pfqPush(queue, current->children[otherdir]);
	    }

	    callback(tree, current, user, 0, 0, &cont, nodenumber++);

	}

    }

    pfqFree(queue);

}

RbNode* rbDumpCallback(RbTree *tree, RbNode *node, void *user, const int bh, const int height, bool *cont, const uint32_t nodenumber) {

    printf("key %d, %s, height %d, black height %d, parent %d%s%s\n",
		    (node==NULL)? 0:node->key,(node == NULL) ? "x " : rbCname(node), height, bh,
		    (node->parent == NULL) ? 0 : node->parent->key,
		    (node->children[RB_LEFT] == NULL && node->children[RB_RIGHT] == NULL) ? ", no children" : "",
		    (node == tree->root) ? ", is root" : ""
    );

    return node;

}

RbNode* rbDummyCallback(RbTree *tree, RbNode *node, void *user, const int bh, const int height, bool *cont, const uint32_t nodenumber) {

    return node;

}

bool rbVerify(RbTree *tree, bool chatty, bool stop) {

    RbVerifyState state = { 0, 0, true, chatty, stop };

    if(tree == NULL) {
	if(chatty) {
	    fprintf(stderr, "Empty tree, valid (NULL is black)\n");
	}
	return true;
    }

    if(rbRed(tree->root)) {
	state.valid = false;
	if(chatty) {
	    fprintf(stderr, "Red root violation\n");
	}
	if(stop) {
	    return false;
	}
    }

    rbInOrderTrack(tree, rbVerifyCallback, &state, RB_ASC);

    if(chatty) {

	if(state.valid) {
	    fprintf(stderr, "Valid red-black tree, node count %d, max height %d, black height %d\n", tree->count, state.maxheight, state.maxbh);
	} else {
	    fprintf(stderr, "Invalid red-black tree.\n");
	}

    }

    return state.valid;

}

/* empty the tree and free it */
void rbFree(RbTree *tree) {

    if(tree != NULL) {
	rbInOrder(tree, rbFreeCallback, NULL, RB_ASC);
	free(tree);
    }

}

/* just empty the tree */
void rbEmpty(RbTree *tree) {

    if(tree != NULL) {
	rbInOrder(tree, rbFreeCallback, NULL, RB_ASC);
    }

}
