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
 * @file   barser.h
 * @date   Sun Apr15 13:40:12 2018
 *
 * @brief  The Bastard Parser
 *
 */

#ifndef BARSER_H_
#define BARSER_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "linked_list.h"
#include "rbt/rbt.h"
#include "barser_defaults.h"

/* BsNode / BsDict is a simple hierarchical data parser,
 * with a tree structure and path-based retrieval (/parent/child/grandchild)
 */


    /* Node types (explained on Haruki's cars):
     *
     * ----- ROOT node: {}
     *
     * Root of the tree. There is only one, cannot be removed from the dictionary,
     * and is added to the dictionary upon creation.
     *
     * ----- BRANCH node:
     *
     * No value, children only: { haruki { carcount 3; } } <- haruki is a branch
     *
     * ----- LEAF node:
     *
     * Value only, no children, 'carcount' above is a leaf
     *
     * ----- ARRAY node:
     *
     * Functionally same as branch, but array items are internally numbered,
     * and displayed differently:
     *
     * { haruki { cars [ camry impreza accord ]; } }
     *
     * the above is equivalent to:
     *
     * { haruki { cars { 0 "camry"; 1 "impreza"; 2 "accord"; } } }
     *
     * the value of an array member is the item listed - the name is always a number.
     *
     * note: arrays require to have a name, unless they are nested in other arrays
     *
     * ----- COLLECTION node:
     *
     * Again functionally same as branch, but holds "instances" and
     * may be parsed and displayed differently:
     *
     * { haruki {
     *		car "yoshi" { model "camry"; } car "toshi" { model "impreza";} car "hoshiboshi" { model "accord"; }
     * }}
     *
     * the above is equivalent to:
     *
     * { haruki {
     *		car { "yoshi" { model "camry"; } "toshi" { model "impreza";} "hoshiboshi" { model "accord"; }}
     * }}
     *
     */

/* min, max, everybody needs min/max */
#ifndef min
#define min(a,b) ((a < b) ? (a) : (b))
#endif

#ifndef max
#define max(a,b) ((a > b) ? (a) : (b))
#endif

/* node types */
enum {
    BS_NODE_ROOT = 0,
    BS_NODE_BRANCH,
    BS_NODE_LEAF,
    BS_NODE_ARRAY,
    BS_NODE_COLLECTION,
};

/* node value types */
enum {
    BS_VAL_NULL,
    BS_VAL_STRING,
    BS_VAL_BOOL,
    BS_VAL_INT,
    BS_VAL_FLOAT
};

/* parser error codes */
enum {
    BS_PERROR_NONE = 0,		/* no error */
    BS_PERROR_EOF,		/* unexpected EOF */
    BS_PERROR_UNEXPECTED,	/* unexpected character */
    BS_PERROR_EXP_ID,		/* expected identifier / name */
    BS_PERROR_UNEXP_ID,		/* unexpected identifier */
    BS_PERROR_TOKENS,		/* too many consecutive identifiers */
    BS_PERROR_LEVEL,		/* unbalanced brackes */
    BS_PERROR_BLOCK,		/* unexpected structure element */
    BS_PERROR_NULL,		/* uninitialised / NULL dictionary */
    BS_PERROR			/* generic / internal / other error */
};

/* node operation result codes */
enum {
    BS_NODE_OK = 0,		/* Whatever went OK */
    BS_NODE_NOT_FOUND,		/* Node not found in dictionary */
    BS_NODE_WRONG_DICT,		/* Node does not belong to this dictionary */
    BS_NODE_EXISTS,		/* Node already exists (i.e. cannot add) */
    BS_NODE_FAIL,		/* All other errors: cannot remove root node, etc */
};

/* shorthand macro to check if (int!) c is a character of class cl (above) */
#define chclass(c, cl) (chflags[(unsigned char)c] & (cl))

typedef struct {
    char* data;
    size_t len;
    unsigned int quoted;
} BsToken;

/* parser state container */
typedef struct {

    /* scanner positions */
    char *current;		/* current character */
    int prev;			/* previous character */
    int c;			/* current character */
    char *end;			/* buffer end marker */

    char *linestart;		/* start position of current line */
    char *slinestart;		/* start position of line when entered state */

    BsToken tokenCache[BS_MAX_TOKENS]; /* token cache */

    size_t linepos;		/* position in line */
    size_t lineno;		/* line number */

    size_t slinepos;		/* line position when entered state */
    size_t slineno;		/* line number when entered state */

    /* scanner state */
    unsigned int scanState;

    /* parser state */
    unsigned int parseEvent;	/* last parse event */
    unsigned int parseError;	/* code of last error */

    /* current adjacent token count */
    unsigned int tokenCount;

} BsState;

typedef struct BsDict BsDict;

typedef struct BsNode BsNode;
struct BsNode {
    char *name;				/* node name */
    char *value;			/* node value */

    LL_HOLDER(BsNode);			/* linked list root */
    LL_MEMBER(BsNode);			/* but also a linked list member */

    BsNode *parent;			/* parent of our node, NULL for root */

    size_t nameLen;			/* name length */
    uint32_t hash;			/* sum of hashes from root to this guy */
    int childCount;			/* fat bastard on benefits and dodgy DLA */
    unsigned int type;			/* node type enum */
    unsigned int flags;			/* flags - quoted name, quoted value, etc. */

#ifdef COLL_DEBUG
    int collcount;			/* temporary, for collision monitoring */
#endif /* COLL_DEBUG */

};

/* dictionary flags */
#define BS_NONE		0		/* also a universal zero constant */
#define BS_NOINDEX	(1<<0)		/* this dictionary instance does not index nodes */
#define BS_READONLY	(1<<1)		/* this dictionary becomes read-only once parsed */

/* node flags */
#define BS_QUOTED_VALUE (1<<0)		/* node name was specified as quoted string */
#define BS_QUOTED_NAME  (1<<1)		/* node value was specified as quoted string */
#define BS_INDEXED	(1<<2)		/* node was indexed */


/* the dictionary */
struct BsDict {
    BsNode *root;		/* root node */
    char *name;			/* well, a name */
    void *index;		/* abstract index */
#ifdef COLL_DEBUG
    int collcount;		/* collision count */
    int maxcoll;		/* maximum collisions to same entry */
#endif /* COLL_DEBUG */
    size_t nodecount;		/* total node count. */
    uint32_t flags;		/* dictionary flags */
};

/*
 * callback type. parameters: dict, node, user, feedback, cont
 *       dict: this node's dictionary
 *       node: this node
 *       user: user data
 *   feedback: Pointer returned by this callback's run on a node
 *             before iterating over its children (preorder traversal),
 *             this allows interaction between the callback running
 *             on a node's parent with the callbacks running on the node.
 *       cont: pointer to a boolean. If the callback sets the underlying
 *             bool to false, iteration stops.
 */
typedef void* (*BsCallback) (BsDict*, BsNode*, void*, void*, bool*);
/* use this if you want, but readability will suffer */
#define BS_CB_ARGS BsDict *dict, BsNode *node, void* user, void* feedback, bool* cont

/* TAKE FILE. PUT FILE IN BUFFER. FILE CAN BE "-" FOR STDIN. RETURN BUFFER */
size_t getFileBuf(char **buf, const char *fileName);

/* create and initialise a dictionary */
BsDict *bsCreate(const char *name, const uint32_t flags);

/* create new node in dictionary, attached to parent, of type type with name name */
BsNode* bsCreateNode(BsDict *dict, BsNode *parent, const unsigned int type, const char* name);

/* clean up and free dictionary */
void bsFree(BsDict *dict);

/* duplicate a dictionary, give new name to resulting dictionary */
BsDict* bsDuplicate(BsDict *source, const char* newname, const uint32_t newflags);

/* parse contents of a char buffer */
BsState bsParse(BsDict *dict, char *buf, size_t len);

/* display parser error */
void bsPrintError(BsState *state);

/* run a callback recursively on node, return node where callback stopped the walk */
BsNode* bsNodeWalk(BsDict *dict, BsNode *node, void* user, void *feedback, BsCallback callback);
/* run a callback recursively on node, passing the node's whole path as feedback on every run */
BsNode* bsNodePWalk(BsDict *dict, BsNode *node, void* user, void *feedback, BsCallback callback);
/* run a callback recursively on dictionary, return node where callback stopped the walk */
BsNode* bsWalk(BsDict *dict, void* user, BsCallback callback);
/* same, with path passed as feedback */
BsNode* bsPWalk(BsDict *dict, void* user, BsCallback callback);

/* output dictionary contents to file */
void bsDump(FILE* fl, BsDict *dict);
/* recursively output node contents to a file, return number of bytes written */
int bsDumpNode(FILE* fl, BsNode *node);

/* retrieve entry from dictionary root based on path */
BsNode* bsGet(BsDict *dict, const char* qry);
/* retrieve entry from dictionary node based on path */
BsNode* bsNodeGet(BsDict* dict, BsNode *node, const char* qry);

/* rename a node and recursively reindex if necessary */
void bsRenameNode(BsDict* dict, BsNode* node, const char* newname);

/*
 * Put BS_PATH_SEP-separated path of given node into out. If out is NULL,
 * required string lenth (including zero-termination) is returned and no
 * extraction is done. Otherwise the length should be passed as @outlen,
 * and path is copied into *out.
 */
size_t bsGetPath(BsNode *node, char* out, const size_t outlen);
/* allocate space on the stack and get node path */
#define BS_GETNP(node, var) size_t var##_size = bsGetPath(node, NULL, 0);\
				    char var[var##_size];\
				    bsGetPath(node, var, var##_size);


#endif /* BARSER_H_ */

