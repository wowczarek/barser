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
     * ----- INSTANCE node:
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

/*
 * Maximum size of token cache Barser will accept. barser_defaults.h
 * defines its own, but this is the global maximum for Barser.
 */

#define BS_MAX_TOKENS 20

/* node types */
enum {
    BS_NODE_ROOT = 0,		/* root node */
    BS_NODE_BRANCH,		/* branch node */
    BS_NODE_LEAF,		/* leaf node */
    BS_NODE_ARRAY,		/* array node */
    BS_NODE_INSTANCE,		/* instance (single child) */
    BS_NODE_VARIABLE,		/* a variable. these are ignored by fetches / queries and only retrieved by bsGetVariable() */
};

/* node value types - for if/when automatic value parsing is implemented */
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
    BS_PERROR_QUOTED,		/* unterminated quoted string */
    BS_PERROR			/* generic / internal / other error */
};

/* node operation result codes (possibly to be removed) */
enum {
    BS_NODE_OK = 0,		/* Whatever went OK */
    BS_NODE_NOT_FOUND,		/* Node not found in dictionary */
    BS_NODE_WRONG_DICT,		/* Node does not belong to this dictionary */
    BS_NODE_EXISTS,		/* Node already exists (i.e. cannot add) */
    BS_NODE_FAIL,		/* All other errors: cannot remove root node, etc */
};

/* shorthand macro to check if (int!) c is a character of class cl (above) */
#define chclass(c, cl) (chflags[(unsigned char)c] & (cl))

/* a string token */
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

    /* token offset - if any "special" / modifier tokens are found and ignored */
    unsigned int tokenOffset;

    /* token cache flags typically set by modifiers */
    unsigned int flags;

} BsState;

typedef struct BsDict BsDict;

typedef struct BsNode BsNode;
struct BsNode {

    char *name;				/* node name */
    char *value;			/* node value */

    BsNode *parent;			/* parent of our node, NULL for root */

    LL_HOLDER(BsNode);			/* linked list root */
    LL_MEMBER(BsNode);			/* but also a linked list member */

    BsNode* _indexNext;			/* singly linked list to hold index chains */

    size_t nameLen;			/* name length */
    size_t valueLen;			/* value length */
    uint32_t hash;			/* sum of hashes from root to this guy */
    unsigned int childCount;		/* fat bastard on benefits and dodgy DLA */
    unsigned int type;			/* node type enum */
    unsigned int flags;			/* flags - quoted name, quoted value, etc. */

#ifdef COLL_DEBUG
    int collcount;			/* temporary, for collision monitoring */
#endif /* COLL_DEBUG */

};

/* node flags */

#define BS_QUOTED_VALUE  (1<<0)		/* node name was specified as quoted string */
#define BS_QUOTED_NAME   (1<<1)		/* node value was specified as quoted string */
#define BS_INDEXED	 (1<<2)		/* node was indexed */
#define BS_MODIFIED	 (1<<3)		/* node contents were changed during merge */
/* parent flags */
#define BS_INACTIVE	 (1<<4)		/* inactive node ("inactive:" modifier)  - exists but ignored */
#define BS_REMOVED	 (1<<5)		/* node was removed during merge */
#define BS_ADDED	 (1<<6)		/* node was added during merge */
#define BS_GENERATED	 (1<<7)		/* node was generated with "generate:" modifier */
/* inherited flags */
#define BS_INACTIVECHLD  (1<<8)		/* descendant of an inactive node */
#define BS_REMOVEDCHLD   (1<<9)		/* descendant of a removed node */
#define BS_ADDEDCHLD     (1<<10)	/* descendant of an added node */
#define BS_GENERATEDCHLD (1<<11)	/* descendant of a generated node */

#define BS_INHERITED_SHIFT 4		/* distance between parent and inherited flags */

/* set of flags inherited from parent - these are shifted to *CHLD for descendants */
#define BS_INHERITED_FLAGS (BS_INACTIVE | BS_REMOVED | BS_ADDED | BS_GENERATED)

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

/* dictionary flags */
#define BS_NONE		0		/* also a universal zero constant */
#define BS_NOINDEX	(1<<0)		/* this dictionary instance does not index nodes */
#define BS_READONLY	(1<<1)		/* this dictionary becomes read-only once parsed */

/*
 * callback type. parameters: dict, node, user, feedback, cont
 *       dict: this node's dictionary
 *       node: this node
 *       user: user data
 *   feedback: Pointer returned by this callback's run on a node
 *             before iterating over its children (preorder traversal),
 *             this allows interaction between the callback running
 *             on a node's parent with the callbacks running on the node.
 *       stop: pointer to a boolean. If the callback sets the underlying
 *             bool to true, iteration stops.
 */
typedef void* (*BsCallback) (BsDict*, BsNode*, void*, void*, bool*);
/* use this if you want, but readability will suffer */
#define BS_CB_ARGS BsDict *dict, BsNode *node, void* user, void* feedback, bool* stop

/* TAKE FILE. PUT FILE IN BUFFER. FILE CAN BE "-" FOR STDIN. RETURN BUFFER */
size_t getFileBuf(char **buf, const char *fileName);

/* create and initialise a dictionary */
BsDict *bsCreate(const char *name, const uint32_t flags);

/* create new node in dictionary, attached to parent, of type type with name name and (optionally) value value */
BsNode* bsCreateNode(BsDict *dict, BsNode *parent, const unsigned int type, const char* name, const char* value);

/* clean up and free dictionary */
void bsFree(BsDict *dict);
/* empty the dictionary */
void bsEmpty(BsDict *dict);
/* free a single node */
void bsFreeNode(BsNode *node);

/* duplicate a dictionary, give new name to resulting dictionary */
BsDict* bsDuplicate(BsDict *source, const char* newname, const uint32_t newflags);
/* copy node to new parent, under (optionally) new name */
BsNode* bsCopyNode(BsDict* dict, BsNode* node, BsNode* newparent, const char* newname);
/* rename a node and recursively reindex if necessary */
BsNode* bsRenameNode(BsDict* dict, BsNode* node, const char* newname);
/* move node from current parent to another, under(optionally) new name */
BsNode* bsMoveNode(BsDict* dict, BsNode* node, BsNode* newparent, const char* newname);

/* parse contents of a char buffer */
BsState bsParse(BsDict *dict, char *buf, size_t len);

/* index all unindexed nodes and enable indexing */
void bsIndex(BsDict* dict);

/* force full reindex - but not a full rehash */
void bsReindex(BsDict *dict);

/* display parser error */
void bsPrintError(BsState *state);

/* output dictionary contents to file */
void bsDump(FILE* fl, BsDict *dict);
/* recursively output node contents to a file, return number of bytes written */
int bsDumpNode(FILE* fl, BsNode *node);

/* retrieve entry from dictionary root based on path */
BsNode* bsGet(BsDict *dict, const char* qry);
/* retrieve entry from dictionary node based on path */
BsNode* bsNodeGet(BsDict* dict, BsNode *node, const char* qry);
/* get (first) parent's child of the given name / check if child exists */
BsNode* bsGetChild(BsDict* dict, BsNode *parent, const char* name);
/* get a list of all parent's children with given name */
LList* bsGetChildren(LList* out, BsDict* dict, BsNode *parent, const char* name);
/* iteratively grab parent's n-th child (starting from 0!) */
BsNode* bsNthChild(BsDict* dict, BsNode *parent, const unsigned int childno);

/* run a callback recursively on node, return node where callback stopped the walk */
BsNode* bsNodeWalk(BsDict *dict, BsNode *node, void* user, void *feedback, BsCallback callback);
/* run a callback recursively on dictionary, return node where callback stopped the walk */
BsNode* bsWalk(BsDict *dict, void* user, BsCallback callback);
/* run a callback recursively on node, passing a BsToken with node's full path as feedback */
BsNode* bsNodePWalk(BsDict *dict, BsNode *node, void* user, void *feedback, BsCallback callback, bool escape);
/* same as bsWalk, but every callback is passed a BsToken as feedback, with node's full path */
BsNode* bsPWalk(BsDict *dict, void* user, BsCallback callback, bool escape);

/* run a callback recursively on node, return linked list that callback permitted */
LList* bsNodeFilter(LList* list, BsDict *dict, BsNode *node, void* user, void *feedback, BsCallback callback);
/* run a callback recursively on dictionary, return linked list that callback permitted */
LList* bsFilter(LList *list, BsDict *dict, void* user, BsCallback callback);
/* run a callback recursively on node, return linked list that callback permitted, callback gets node path */
LList* bsNodePFilter(LList *list, BsDict *dict, BsNode *node, void* user, void *feedback, BsCallback callback, bool escape);
/* run a callback recursively on dictionary, return linked list that callback permitted, callback gets node path */
LList* bsPFilter(LList *list, BsDict *dict, void* user, BsCallback callback, bool escape);

/* callback for use with bsFilter, checking if node value contains string */
void* bsValueContainsCb(BsDict *dict, BsNode *node, void* user, void* feedback, bool* matches);
/* callback for use with bsFilter, checking if node value contains string */
void* bsNameContainsCb(BsDict *dict, BsNode *node, void* user, void* feedback, bool* matches);

/*
 * Put BS_PATH_SEP-separated path of given node into out. If out is NULL,
 * required string lenth (including zero-termination) is returned and no
 * extraction is done. Otherwise the length should be passed as @outlen,
 * and path is copied into *out. The escaped versions add escape characters
 * to path elements, leaving the '/' intact.
 */
size_t bsGetPath(BsNode *node, char* out, const size_t outlen);
/* allocate space on the stack and get node path */
#define BS_GETNP(node, var) size_t var##_size = bsGetPath(node, NULL, 0);\
				    char var[var##_size];\
				    bsGetPath(node, var, var##_size);
size_t bsGetEscapedPath(BsNode *node, char* out, const size_t outlen);
/* allocate space on the stack and get node path */
#define BS_GETENP(node, var) size_t var##_size = bsGetEscapedPath(node, NULL, 0);\
				    char var[var##_size];\
				    bsGetEscapedPath(node, var, var##_size);

/* various helper functions */

/* unescape a string in place and return new size including NUL-termination */
size_t bsUnescapeStr(char *str);
/* place escaped string in @out, return required size (also if @out is 0) including NUL-termination */
size_t bsEscapeStr(const char *str, char *out);
/* get an escaped duplicate of string src */
char* bsGetEscapedStr(const char* src);

/* this is a test call to remain here until development is done */
bool bsTest(BsDict *dict);

#endif /* BARSER_H_ */

