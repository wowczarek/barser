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
#include "linked_list.h"

/* BarserNode / BarserDict is a simple hierarchical data parser,
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
     * ----- PROPERTYLIST node:
     *
     * Property list type node may only belong to a collection, and functions like a branch or leaf, but
     * may be displayed along with the collection and instance names:
     *
     * { haruki {
     * 	    car yoshi age 6;
     * 	    car toshi age 9;
     * 	    car hoshiboshi age 0.5;
     * 	}}
     *
     * the above is equivalent to:
     *
     * { haruki {
     * 	    car yoshi { age 6 };
     * 	    car toshi { age 9 };
     * 	    car hoshiboshi { age 0.5 };
     * 	}}
     *
     * or:
     *
     * { haruki {
     * 	    car { "yoshi" { age 6 }; "toshi" { age 9 }; "hoshiboshi" { age 0.5 }; }
     * 	}}
     *
     */

/* node types */
enum {
    BP_NODE_BRANCH = 0,
    BP_NODE_LEAF,
    BP_NODE_ARRAY,
    BP_NODE_COLLECTION,
    BP_NODE_PROPERTYLIST,
    BP_NODE_ROOT
};

/* parser error codes */
enum {
    BP_PERROR_NONE = 0,		/* no error */
    BP_PERROR_EOF,		/* unexpected EOF */
    BP_PERROR_UNEXPECTED,	/* unexpected character */
    BP_PERROR_EXP_ID,		/* expected identifier / name */
    BP_PERROR_UNEXP_ID,		/* unexpected identifier */
    BP_PERROR_TOKENS,		/* too many consecutive identifiers */
    BP_PERROR_LEVEL,		/* unbalanced brackes */
    BP_PERROR_BLOCK,		/* unexpected structure element */
    BP_PERROR_NULL,		/* uninitialised / NULL dictionary */
    BP_PERROR			/* generic / internal / other error */
};

/* node operation result codes */
enum {
    BP_NODE_OK = 0,		/* Whatever went OK */
    BP_NODE_NOT_FOUND,		/* Node not found in dictionary */
    BP_NODE_WRONG_DICT,		/* Node does not belong to this dictionary */
    BP_NODE_EXISTS,		/* Node already exists (i.e. cannot add) */
    BP_NODE_FAIL,		/* All other errors: cannot remove root node, etc */
};

/* shorthand macro to check if (int!) c is a character of class cl (above) */
#define chclass(c, cl) (chflags[c] & (cl))

/* parser state container */
typedef struct {

    /* scanner positions */
    char *current;		/* current character */
    int prev;			/* previous character */
    char *end;			/* buffer end marker */

    char *str;			/* last token grabbed */
    char *strstart;		/* start position of last string token */
    char *linestart;		/* start position of current line */

    char *slinestart;		/* start position of line when entered state */

    size_t linepos;		/* position in line */
    size_t lineno;		/* line number */

    size_t slinepos;		/* line position when entered state */
    size_t slineno;		/* line number when entered state */

    /* scanner state */
    unsigned int scanState;

    /* parser state */
    unsigned int parseState;	/* parser state */
    unsigned int parseEvent;	/* last parse event */
    unsigned int parseError;	/* code of last error */

    /* current adjacent token count */
    unsigned int tokenCount;

} BarserState;

typedef struct BarserDict BarserDict;

typedef struct BarserNode BarserNode;
struct BarserNode {
    char *name;				/* node name */
    char *value;			/* node value */

    LL_HOLDER(BarserNode);		/* linked list root */
    LL_MEMBER(BarserNode);		/* but also a linked list member */

    BarserNode *parent;			/* parent of our node, NULL for root */
    BarserDict *dict;			/* dictionary pointer */

    size_t _pathLen;			/* path length */
    int childCount;			/* fat bastard on benefits and dodgy DLA */
    unsigned int type;			/* type enum */
    unsigned int flags;			/* flags - quoted name, quoted value, etc. */
};

/* node flags */
#define BP_QUOTED_VALUE	(1<<0)
#define BP_QUOTED_NAME	(1<<1)

/* the dictionary */
struct BarserDict {
    BarserNode *root;		/* root node */
    char *name;			/* well, a name */
};

size_t getFileBuf(char **buf, const char *fileName);

/* create and initialise a dictionary */
BarserDict *createBarserDict(const char *name);

/* create new node in dictionary, attached to parent, of type type with name name */
BarserNode* createBarserNode(BarserDict *dict, BarserNode *parent, const unsigned int type, const char* name);

/* clean up and free dictionary */
void freeBarserDict(BarserDict *dict);

/* parse contents of a char buffer */
BarserState barseBuffer(BarserDict *dict, char *buf, size_t len);

/* display parser error */
void printBarserError(BarserState *state);

/* recursively dump a node (like the root) */
void dumpBarserNode(BarserNode *node, int level);

#endif /* BARSER_H_ */

