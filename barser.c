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
 * @file   barser.c
 * @date   Sun Apr15 13:40:12 2018
 *
 * @brief  Configuration file parser and searchable dictionary
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdbool.h>

#include "rbt/st_inline.h"

#include "xalloc.h"
#include "xxh.h"
#include "itoa.h"

#include "barser.h"
#include "barser_index.h"

#include "barser_defaults.h"

/*
 * if the defaults file wants to handle a smaller token cache,
 * let it have it, otherwise the barser.h constant is in effect
 */
#if (BS_BUILD_MAX_TOKENS < BS_MAX_TOKENS)

#undef BS_MAX_TOKENS
#define BS_MAX_TOKENS BS_BUILD_MAX_TOKENS

#endif /* (BS_BUILD_MAX_TOKENS < BS_MAX_TOKENS) */

/* stdin block size */
#define BS_STDIN_BLKSIZE 2048
/* stdin block growth */
#define BS_STDIN_BLKEXTENT 10
/* root node hash - a large 32-bit prime with a healthy bit mix */
#define BS_ROOT_HASH 0xace6cabd

/* hash mixing function */
#define BS_MIX_HASH(a, b, len) ((a ^ rol32(b, 31)))
/* an alternative */
/* #define BS_MIX_HASH(a, b, len) (rol32(a, 1) + rol32(b, 7)) */

/* declare a string buffer of given length (+1) and initialise it */
#ifndef tmpstr
#define tmpstr(name, len) char name[len + 1];\
int name ## _len = len + 1;\
memset(name, 0, name ## _len);
#endif

/* clear a string buffer (assuming _len suffix declared using tmpstr() */
#ifndef clearstr
#define clearstr(name)\
memset(name, 0, name ## _len);
#endif

/* size of a string to fit an int safely - log 2(10) ~ 3, room for error, sign, NUL */
#ifndef INT_STRSIZE
#define INT_STRSIZE (3 * sizeof(int) + 3)
#endif

/* shorthand to dump the token stack contents */
#define tdump()\
		fprintf(stderr, "Token cache with %d items: ", state.tokenCount);\
		for(int i = 0; i < state.tokenCount; i++) {\
		    fprintf(stderr, "'");\
		    if(state.tokenCache[i].data != NULL) {\
			for(int j = 0; j < state.tokenCache[i].len; j++) {\
			    fprintf(stderr, "%c", *(state.tokenCache[i].data + j));\
			}\
		    }\
		    fprintf(stderr, "' ");\
		}\
		fprintf(stderr, "\n");

/* shorthand to 'safely' clean up the token cache */
#define tokencleanup()\
		for(int i = 0; i < BS_MAX_TOKENS; i++) {\
		    if(state.tokenCache[i].quoted && state.tokenCache[i].data != NULL) {\
			free(state.tokenCache[i].data);\
			state.tokenCache[i].data = NULL;\
		    }\
		}\
		state.tokenCount = 0;\
		state.tokenOffset = 0;

/* shorthant to reset token cache */
#define tokenreset() \
		state.tokenCount = 0;\
		state.tokenOffset = 0;\
		state.flags = 0;

/* shorthand to get token data and quoted check flags */
#define td(n) getTokenData(&state.tokenCache[n + state.tokenOffset])
#define ts(n) state.tokenCache[n + state.tokenOffset].data
#define tq(n) state.tokenCache[n + state.tokenOffset].quoted
#define tl(n) state.tokenCache[n + state.tokenOffset].len

/* get the existing child of node 'parent' named as token #n in cache */
#define gch(parent, n) _bsGetChild(dict, parent, state.tokenCache[n].data, state.tokenCache[n].len)

/* string scan state machine */
enum {
    BS_NOOP = 0,		/* do nothing (?) */
    BS_SKIP_WHITESPACE,		/* skipping whitespace (newlines are part of this) */
    BS_SKIP_NEWLINE,		/* skipping newlines when explicitly required */
    BS_GET_TOKEN,		/* acquiring a token */
    BS_GET_QUOTED,		/* acquiring a quoted / escaped string */
    BS_SKIP_COMMENT,		/* skipping a comment until after next newline */
    BS_SKIP_MLCOMMENT		/* skipping a multiline comment until end of comment */
};

/* parser events */
enum {
    BS_NOEVENT = 0,		/* nothing happened, keep scanning */
    BS_GOT_TOKEN,	/* received a string */
    BS_GOT_ENDVAL,	/* received "end of value", such as ';' */
    BS_GOT_BLOCK,	/* received beginning of block, such as '{' */
    BS_END_BLOCK,	/* received end of block, such as '}' */
    BS_GOT_ARRAY,	/* received opening of an array, such as '[' */
    BS_END_ARRAY,	/* received end of array, such as ']' */
    BS_GOT_EOF,		/* received EOF */
    BS_ERROR		/* parse error */
};

/* 'c' class check shorthand, assumes the presence of 'c' int variable */
#define cclass(cl) (chflags[(unsigned char)c] & (cl))

/* save state when we encounter a section that can have unmatched or unterminated bounds */
#define savestate(st) 		st->slinestart = st->linestart;\
				st->slineno = st->lineno;\
				st->slinepos = st->linepos;
/* restore state, say when printing an error */
#define restorestate(st) 	st->linestart = st->slinestart;\
				st->lineno = st->slineno;\
				st->linepos = st->slinepos;


/* ========= static function declarations ========= */

/* initialise parser state */
static void bsInitState(BsState *state, char* buf, const size_t bufsize);
/* return a pointer to token data / name, duplicating / copying if necessary */
static inline char* getTokenData(BsToken *token);
/* fetch next character from buffer and advance, return it as int or EOF if end reached */
static inline int bsForward(BsState *state);
/* peek at the next character without moving forward */
static inline int bsPeek(BsState *state);

/* dump a quoted string (if quoted) and escape characters where needed */
static inline int bsDumpQuoted(FILE* fl, char *src, bool quoted);
/* Dump node contents recursively to a file pointer. */
static int _bsDumpNode(FILE* fl, BsNode *node, int level);
/* Create a node in dict at given parent with given name and (optionally) value */
static inline BsNode* _bsCreateNode(BsDict *dict, BsNode *parent,
			const unsigned int type, char* name,
			const size_t namelen, char* value, size_t valuelen);
/* [get|check if] parent node has a child with specified name */
static inline BsNode* _bsGetChild(BsDict* dict, BsNode *parent,
			const char* name, const size_t namelen);
/* get a list of children of node with specified name. Returns a dynamic LList* that needs freed */
static inline LList* _bsGetChildren(LList* out, BsDict* dict, BsNode *parent,
			const char* name, const size_t namelen);
/* walk through string @in, and write to + return next token between the 'sep' character */
static inline BsToken* unescapeToken(BsToken* out, char** in, const char sep);
/* recursive node rehash callback */
static void* bsRehashCallback(BsDict *dict, BsNode *node, void* user, void* feedback, bool* stop);
/* print error hint from state structure */
static void bsErrorHint(BsState *state);
/* main buffer scanner / lexer state machine */
static inline void bsScan(BsState *state);
/* node indexing callback - used when indexing a previously unindexed dictionary */
static void* bsIndexCallback(BsDict *dict, BsNode *node, void* user, void* feedback, bool* stop);
/* node reindexing callback - used when forcing a reindex */
static void* bsReindexCallback(BsDict *dict, BsNode *node, void* user, void* feedback, bool* stop);
/* expand escape sequences and produce a clean query trimmed on both ends, matching the bsGetPath output */
static inline size_t cleanupQuery(char* query);
/* return a new string containing cleaned up query */
static inline char* getCleanQuery(const char* query);
/* compute the compound hash of a query path rooted ad node @root */
static inline uint32_t bsGetPathHash(BsNode* root, const char* query);
/* dictionary / node duplication callback */
static void *bsDupCallback(BsDict *dict, BsNode *node, void* user, void* feedback, bool* stop);

/* ========= function definitions ========= */

/* initialise parser state */
static void bsInitState(BsState *state, char* buf, const size_t bufsize) {

    state->current = buf;
    state->prev = '\0';
    state->c = buf[0];
    state->end = buf + bufsize;

    state->linestart = buf;
    state->slinestart = buf;

    memset(&state->tokenCache, 0, BS_MAX_TOKENS * sizeof(BsToken));

    state->linepos = 0;
    state->lineno = 1;

    state->slinepos = 0;
    state->slineno = 1;

    state->scanState = BS_SKIP_WHITESPACE;

    state->parseEvent = BS_NOEVENT;
    state->parseError = BS_PERROR_NONE;

    state->tokenCount = 0;
    state->tokenOffset = 0;
    state->flags = BS_NONE;

}


/* return a pointer to token data / name, duplicating / copying if necessary */
static inline char* getTokenData(BsToken *token) {

    char* out;

    /*
     * a quoted string is always dynamically allocated to we can take it as is,
     * we only need to trim it, because they are resized by 2 when parsing,
     * so get rid of extra memory by cutting it to the required size only.
     */
    if(token->quoted) {

	xrealloc(out, token->data, token->len + 1);
	/* this way we know this has been used, so we will not attempt to free it */
	token->data = NULL;

    /* otherwise token->data is in an existing buffer, so we duplicate */
    } else {

	xmalloc(out, token->len + 1);
	memcpy(out, token->data, token->len);

    }

    /* good boy! */
    out[token->len] = '\0';

    return out;

}

/* fetch next character from buffer and advance, return it as int or EOF if end reached */
static inline int bsForward(BsState *state) {

    int c;

    if(state->current >= state->end) {
        return EOF;
    }

    state->prev = state->c;

    state->current++;
    c = *state->current;

    if(c == '\0') {
        state->c = EOF;
        return EOF;
    }

    /* we have a newline */
    if(cclass(BF_NLN)) {

	/*
	 * If we came across two different newline characters, advance line number only once.
	 * This is a cheap trick to handle Windows' CR-LF. Let me know if this even lands on Window.
	 */
	if(!(chflags[state->prev] & BF_NLN) || c == state->prev) {
	    state->linestart = state->current + 1;
	    state->lineno++;
	    state->linepos = 0;
	}

    } else {
	state->linepos++;
    }

    return (state->c = c);

}

/* peek at the next character without moving forward */
static inline int bsPeek(BsState *state) {

    if(state->current >= state->end) {
	return EOF;
    }

    return (int)*(state->current + 1);

}

/* allocate buffer. put file in buffer. return size. file can be '-' */
size_t getFileBuf(char **out, const char *fileName) {

    char *buf = NULL;
    size_t size = 0;
    FILE* fl;

    /* read from stdin in blocks */
    if(!strncmp(fileName, "-", 1)) {

	size_t bufsize = BS_STDIN_BLKSIZE;

	xmalloc(buf, bufsize + 1);
	if(buf == NULL) {
	    goto failure;
	}

	while(!feof(stdin)) {

	    size_t got = fread(buf + size, 1, BS_STDIN_BLKSIZE, stdin);
	    size += got;

	    /* if we got less than block size, we have probably reached the end */
	    if(got < BS_STDIN_BLKSIZE) {
		/* shrink to fit */
		xrealloc(buf, buf, size + 1);
	    /* otherwise grow, realloc */
	    } else if(size == bufsize) {
		bufsize += BS_STDIN_BLKEXTENT * BS_STDIN_BLKSIZE;
		xrealloc(buf, buf, bufsize + 1);
	    }

	}

    } else {

	/* open file, seek to end, ftell to know the size */
	if((fl = fopen(fileName, "r")) == NULL || fseek(fl, 0, SEEK_END) < 0 || (size = ftell(fl)) < 0) {
	    goto failure;
	}

	rewind(fl);

	xmalloc(buf, size + 1);

	size_t ret = fread(buf, 1, size, fl);

	if(ret < size) {

	    if(feof(fl)) {
		fprintf(stderr, "Error: Truncated data while reading '%s': file size %zu bytes, read %zu bytes\n", fileName, size, ret);
	    }

	    if(ferror(fl)) {
		fprintf(stderr, "Error while reading '%s': file size %zu bytes, read %zu bytes\n", fileName, size, ret);
	    }

	    goto failure;

	}

	fclose(fl);

    }

    *out = buf;
    buf[size] = '\0';
    return(size + 1);

failure:

    if(buf != NULL) {
	free(buf);
    }

    *out = NULL;
    return 0;

}

/* dump a quoted string (if quoted) and escape characters where needed */
static inline int bsDumpQuoted(FILE* fl, char *src, bool quoted) {

    int ret;
    int c;

    if(quoted) {

	ret = fprintf(fl, "%c", BS_QUOTE_CHAR);
	if(ret < 0) {
	    return -1;
	    }

	for(char *marker = src; c = *marker, c != '\0'; marker++) {
	    /* since we only print with double quotes, do not escape other quotes */
	    if(cclass(BF_ESC)
#ifdef BS_QUOTE1_CHAR
		&& c != BS_QUOTE1_CHAR
#endif
#ifdef BS_QUOTE2_CHAR
		&& c != BS_QUOTE2_CHAR
#endif
#ifdef BS_QUOTE2_CHAR
		&& c != BS_QUOTE3_CHAR
#endif
	    ) {
		ret = fprintf(fl, "%c%c", BS_ESCAPE_CHAR, esccodes[c]);
		if(ret < 0) {
		    return -1;
		}
	    } else {
		    ret = fprintf(fl, "%c", c);
		    if(ret < 0) {
			return -1;
		    }
	    }
	}

	ret = fprintf(fl, "%c", BS_QUOTE_CHAR);

    } else {
	ret = fprintf(fl, "%s", src);
	if(ret < 0) {
	    return -1;
	}
    }

    return 0;

}

/*
 * This is so inconceivably fugly it makes me want to take a rusty screwdriver
 * to my neck and stab myself repeatedly with it. But it will have to do for now.
 *
 * Dump node contents recursively to a file pointer.
 */
static int _bsDumpNode(FILE* fl, BsNode *node, int level)
{
    bool noIndentArray = true;

    /* allocate enough indentation space for current level + 1 */
    int maxwidth = (level + 1) * BS_INDENT_WIDTH;
    char indent[ maxwidth + 1];
    BsNode *n = NULL;
    bool inArray = (node->parent != NULL && node->parent->type == BS_NODE_ARRAY);
    bool isArray = (node->type == BS_NODE_ARRAY);
    bool hadBranchSibling = inArray && node->_prev != NULL && node->_prev->type != BS_NODE_LEAF;

    /* fill up the buffer with indent char, but up to current level only */
    memset(indent, BS_INDENT_CHAR, maxwidth);
    memset(indent + level * BS_INDENT_WIDTH, '\0', BS_INDENT_WIDTH);
#ifdef COLL_DEBUG
    fprintf(fl, "\n// hash: 0x%08x\n", node->hash);
#endif /* COLL_DEBUG */
    /* yessir... */
    indent[maxwidth] = '\0';

	fprintf(fl, "%s", inArray && noIndentArray && !hadBranchSibling ? " " : indent);

	if(node->parent != NULL) {

	    if(!inArray) {

		if(node->flags & BS_INACTIVE) {
		    fprintf(fl, "inactive: ");
		}
		
		bsDumpQuoted(fl, node->name, node->flags & BS_QUOTED_NAME);

		if(node->type == BS_NODE_INSTANCE) {
		    fprintf(fl, " ");

		    node = node->_firstChild;
		    inArray = (node->parent != NULL && node->parent->type == BS_NODE_ARRAY);
		    isArray = (node->type == BS_NODE_ARRAY);

		    bsDumpQuoted(fl, node->name, node->flags & BS_QUOTED_NAME);

		    if(node->childCount == 1) {
			BsNode *tmp = (BsNode*)node->_firstChild;
			if(tmp != NULL && tmp->type == BS_NODE_LEAF) {
			    fprintf(fl, " ");

			    bsDumpQuoted(fl, tmp->name, tmp->flags & BS_QUOTED_NAME);

			    if(tmp->value != NULL) {
				fprintf(fl, " ");

				bsDumpQuoted(fl, tmp->value, tmp->flags & BS_QUOTED_VALUE);

			    }

			    fprintf(fl, "%c\n", BS_ENDVAL_CHAR);

			    return 0;
			}

		    }


		}

	    }
	}

    if(node->childCount == 0) {
	if(node->type != BS_NODE_ROOT) {

	    if(node->value && node->valueLen > 0) {

		if(!inArray) {
		    fprintf(fl, " ");
		}

		bsDumpQuoted(fl, node->value, node->flags & BS_QUOTED_VALUE);

		if(!inArray) {
		    fprintf(fl, "%c", BS_ENDVAL_CHAR);
		}

		if(!inArray) {
		    fprintf(fl, "\n");
		}
	    } else {
		if(!inArray) {
		    fprintf(fl, "%c", BS_ENDVAL_CHAR);
		}
		if(!isArray || !noIndentArray) {
		    fprintf(fl, "\n");
		}

	    }
	
	}

    } else {

	if(node->type != BS_NODE_ROOT) {
		fprintf(fl,  "%s%c", strlen(node->name) ? " " : "",
		    isArray ? BS_STARTARRAY_CHAR : BS_STARTBLOCK_CHAR);

		if(!isArray || !noIndentArray) {
		    fprintf(fl, "\n");
		}
	}

	/* increase indent in case if we want to print something here later */
	memset(indent + level * BS_INDENT_WIDTH, BS_INDENT_CHAR, BS_INDENT_WIDTH);
	LL_FOREACH_DYNAMIC(node, n) {

		if(_bsDumpNode(fl, n, level + (node->parent != NULL)) < 0) {
		    return -1;
		}
	}

	/* decrease indent again */
	memset(indent + (level) * BS_INDENT_WIDTH, '\0', BS_INDENT_WIDTH);
	if(node->type != BS_NODE_ROOT) {

		fprintf(fl, "%s", isArray && noIndentArray ? " " : indent);

		if(isArray) {

		    fprintf(fl, "%c", BS_ENDARRAY_CHAR);

		    if(!inArray) {
			fprintf(fl, "%c", BS_ENDVAL_CHAR);
		    }
		} else {
		    fprintf(fl, "%c", BS_ENDBLOCK_CHAR);

		}

	}

	fprintf(fl, "\n");

    }

    if(ferror(fl)) {
	return -1;
    }

    return 0;

}

/* dump a single node recursively to file, return number of bytes written */
int bsDumpNode(FILE* fl, BsNode *node) {
    if(node == NULL) {
	return fprintf(fl, "null\n");
    }
    return _bsDumpNode(fl, node, 0);
}

/* dump the whole dictionary */
void bsDump(FILE* fl, BsDict *dict) {

    _bsDumpNode(fl, dict->root, 0);

}

/* free a single node */
void bsFreeNode(BsNode *node)
{

    if(node == NULL) {
	return;
    }

    if(node->name != NULL) {
	free(node->name);
    }
    if(node->value != NULL) {
	free(node->value);
    }

    free(node);

}

/*
 * Create a node in dict with parent parent of type type using name 'name' of length 'namelen',
 * and with a value of 'value' and length 'valuelen'. This is the internal version of this function
 * (underscore), which only attaches the name + value to the node. If called directly,
 * the name should have been passed throuh getTokenData() first or otherwise be malloc'd,
 * so that the name is guaranteed not to come from a buffer that will later be destroyed.
 * If zero lengths are given for namelen or valuelen, strlen() is performed.
 */
static inline BsNode* _bsCreateNode(BsDict *dict, BsNode *parent, const unsigned int type, char* name, const size_t namelen, char* value, size_t valuelen)
{

    BsNode *ret;
    size_t slen = 0;
    size_t vlen = 0;

    if(dict == NULL) {
	return NULL;
    }

    xmalloc(ret, sizeof(BsNode));

    /* could have calloc'd, but... */

    ret->value = NULL;
    ret->parent = parent;

    ret->_indexNext = NULL;
    LL_CLEAR_HOLDER(ret);
    LL_CLEAR_MEMBER(ret);

    ret->nameLen = 0;
    ret->valueLen = 0;
    ret->childCount = 0;
    ret->type = type;
    ret->flags = 0;
#ifdef COLL_DEBUG
    ret->collcount = 0;
#endif /* COLL_DEBUG */
    if(parent != NULL) {

	/* inherit flags */

	/* parent's inheritable flags shifted */
	unsigned int iflags = (parent->flags & BS_INHERITED_FLAGS) << BS_INHERITED_SHIFT;
	/* also apply parent's already shifted inherited flags */
	iflags |= parent->flags & (BS_INHERITED_FLAGS << BS_INHERITED_SHIFT);

	ret->flags |= iflags;

	/* if we are adding an array member, call it by number, ignoring the name */
	if(parent->type == BS_NODE_ARRAY) {
	    char numname[INT_STRSIZE + 1];
	    /* major win over snprintf, 30% total performance difference for citylots.json */
	    char* endname = u32toa(numname, parent->childCount);
	    slen = endname - numname;
	    xmalloc(ret->name, slen + 1);
	    memcpy(ret->name, numname, slen + 1);
	} else {
	    if(name == NULL) {
		goto onerror;
	    }

	    ret->name = name;

	    if(namelen == 0) {
		slen = strlen(name);
	    } else {
		slen = namelen;
	    }
	}

	ret->value = value;

	if(value != NULL && valuelen == 0) {
	    vlen = strlen(value);
	} else {
	    vlen = valuelen;
	}

	/* mix this node's name's hash with parent's hash */
	ret->hash = BS_MIX_HASH(xxHash32(ret->name, slen), parent->hash, slen);

#if 0
	/* if this is an instance, also mix it with value */
	if(type == BS_NODE_INSTANCE) {
	    ret->hash = BS_MIX_HASH(xxHash32(ret->value, vlen), ret->hash, vlen);
	}
#endif
	ret->nameLen = slen;
	ret->valueLen = vlen;

	if(!(dict->flags & BS_NOINDEX)) {
	    bsIndexPut(dict, ret);
	}

	LL_APPEND_DYNAMIC(parent, ret);
	parent->childCount++;

    } else {

	if(dict->root != NULL) {
	    fprintf(stderr, "Error: will not replace dictionary '%s' root with node '%s'\n", dict->name, name);
	    goto onerror;
	} else {
	    dict->root = ret;
	    ret->type = BS_NODE_ROOT;
	    xmalloc(ret->name, 1);
	    ret->name[0] = '\0';
	    ret->hash = BS_ROOT_HASH;
	}

    }

    dict->nodecount++;
    return ret;

onerror:

    bsFreeNode(ret);
    return NULL;

}

/*
 * Public node creation wrapper.
 *
 * Create a new node in @dict, attached to @parent, of type @type with name @name.
 * If the parent is an array, we do not need a name. If it's not, the name is duplicated
 * and length is calculated.
 */
BsNode* bsCreateNode(BsDict *dict, BsNode *parent, const unsigned int type, const char* name, const char *value) {

    char* nout = NULL;
    size_t nlen = 0;
    char* vout = NULL;
    size_t vlen = 0;

    if(parent == NULL) {
	return NULL;
    }

    if(value != NULL) {

	/* only leafs and instances can have values */
	if(type != BS_NODE_LEAF) {
	    return NULL;
	}

        vlen = strlen(name);
        xmalloc(vout, vlen + 1);
	if(vlen > 0) {
    	    memcpy(vout, value, vlen);
	}
	*(vout + vlen) = '\0';
    }

    if(parent != NULL && parent->type == BS_NODE_ARRAY) {

	return _bsCreateNode(dict, parent, type, NULL, 0, vout, vlen);

    }  else {

	if(name == NULL || ((nlen = strlen(name)) == 0)) {
	    xmalloc(nout, 1);
	} else {
    	    nlen = strlen(name);
    	    xmalloc(nout, nlen + 1);
    	    memcpy(nout, name, nlen);
	}

	*(nout + nlen) = '\0';

	return _bsCreateNode(dict, parent, type, nout, nlen, vout, vlen);
    }

}

/* [get|check if] parent node has a child with specified name */
static inline BsNode* _bsGetChild(BsDict* dict, BsNode *parent, const char* name, const size_t namelen) {

    uint32_t hash;
    BsNode *n, *m;

    if(name != NULL && namelen > 0) {

	hash = BS_MIX_HASH(xxHash32(name, namelen), parent->hash, namelen);

	/* grab node from index if we can */
	if(!(dict->flags & BS_NOINDEX)) {

	    /* if we wanted to do a Robin Hood, bsIndexGet() would have to be rewritten to do this part */
	    for(n = bsIndexGet(dict->index, hash); n != NULL; n = n->_indexNext) {
		if(n->parent == parent && n->nameLen == namelen && !strncmp(name, n->name, namelen)) {
		    return n;
		}
	    }

	/* otherwise do a naive search */
	} else {

	    /*
	     * (todo: investigate skip lists - but that would be an index)
	     * for now, search from both ends of the list simultaneously.
	     */

	    n = parent->_firstChild;
	    m = parent->_lastChild;

	    /* until we meet */
	    while( m != NULL && n != NULL) {

		if(n->hash == hash && n->nameLen == namelen && !strncmp(name, n->name, namelen)) {
		    return n;
		}

		/* we've met */
		if(m == n) {
		    break;
		}

		if(m->hash == hash && m->nameLen == namelen && !strncmp(name, m->name, namelen)) {
		    return m;
		}

		n = n->_next;

		/* we're about to pass each other */
		if(m == n) {
		    break;
		}

		m = m->_prev;

	    }
	
	}

    }

    return NULL;

}

/* get a list of children of node with specified name. Returns a dynamic LList* that needs freed */
static inline LList* _bsGetChildren(LList* out, BsDict* dict, BsNode *parent, const char* name, const size_t namelen) {

    uint32_t hash;
    BsNode *n, *m;

    if(out == NULL) {
	out = llCreate();
    }

    if(name != NULL && namelen > 0) {

	hash = BS_MIX_HASH(xxHash32(name, namelen), parent->hash, namelen);

	/* grab node from index if we can */
	if(!(dict->flags & BS_NOINDEX)) {

	    /* if we wanted to do a Robin Hood, bsIndexGet() would have to be rewritten to do that (put last item in front) */
	    for(n = bsIndexGet(dict->index, hash); n != NULL; n = n->_indexNext) {
		if(n->parent == parent && n->nameLen == namelen && !strncmp(name, n->name, namelen)) {
		    llAppendItem(out, n);
		}
	    }

	/* otherwise do a naive search */
	} else {

	    /*
	     * (todo: investigate skip lists - but that would be an index)
	     * for now, search from both ends of the list simultaneously.
	     */

	    n = parent->_firstChild;
	    m = parent->_lastChild;

	    /* until we meet */
	    while( m != NULL && n != NULL) {

		if(n->hash == hash && n->nameLen == namelen && !strncmp(name, n->name, namelen)) {
		    llAppendItem(out, n);
		}

		/* we've met */
		if(m == n) {
		    break;
		}

		if(m->hash == hash && m->nameLen == namelen && !strncmp(name, m->name, namelen)) {
		    llAppendItem(out, m);
		}

		n = n->_next;

		/* we're about to pass each other */
		if(m == n) {
		    break;
		}

		m = m->_prev;

	    }
	
	}

    }

    return out;

}


/* delete node from the dictionary */
unsigned int bsDeleteNode(BsDict *dict, BsNode *node)
{

    if(node == NULL) {
	return BS_NODE_NOT_FOUND;
    }

    /* remove node from index */
    if(!(dict->flags & BS_NOINDEX)) {
	bsIndexDelete(dict->index, node);
    }

    /* remove all children recursively first */
    for ( BsNode *child = node->_firstChild; child != NULL; child = node->_firstChild) {
	bsDeleteNode(dict, child);
    }

    /* root node is persistent, otherwise remove node */
    if(node->parent != NULL) {
	LL_REMOVE_DYNAMIC(node->parent, node); /* remove self from parent's list */
	node->parent->childCount--;
	bsFreeNode(node);
    }

    dict->nodecount--;

    return BS_NODE_OK;
}

/* create a (named) dictionary */
BsDict* bsCreate(const char *name, const uint32_t flags) {

    size_t slen = 0;
    BsDict *ret;

    xcalloc(ret, 1, sizeof(BsDict));

    /* because no strdup() */
    if(name == NULL || ((slen = strlen(name)) == 0)) {
	xmalloc(ret->name, 1);
	ret->name[0] = '\0';
    } else {
	xmalloc(ret->name, slen + 1);
	memcpy(ret->name, name, slen);
    }

    *(ret->name + slen) = '\0';

    /* set flags */
    ret->flags = flags;

    /* create the root node */
    _bsCreateNode(ret, NULL, BS_NODE_ROOT,NULL,0,NULL,0);

    /* create the index */
    if(!(flags & BS_NOINDEX)) {
	ret->index = bsIndexCreate();
	if(ret->index == NULL) {
	    bsFree(ret);
	    return NULL;
	}
    }

    return ret;
}

void bsEmpty(BsDict *dict) {

    if(dict == NULL) {
	return;
    }

    /* if the dictionary is indexed, free the dictionary on index level */
    if(!(dict->flags & BS_NOINDEX) && dict->index != NULL) {
	bsIndexFree(dict->index);
    /* otherwise delete nodes recursively */
    } else {
	bsDeleteNode(dict, dict->root);
    }

    LL_CLEAR_HOLDER(dict->root);
    LL_CLEAR_MEMBER(dict->root);

    dict->root->childCount = 0;
#ifdef COLL_DEBUG
    dict->root->collcount = 0;
#endif /* COLL_DEBUG */

}

/* free a dictionary */
void bsFree(BsDict *dict) {

    if(dict == NULL) {
	return;
    }

    bsEmpty(dict);

    if(dict->root != NULL) {
	bsFreeNode(dict->root);
    }

    if(dict->name != NULL) {
	free(dict->name);
    }

    free(dict);

}

/* unescape a string in place and return new length including NUL-termination */
inline size_t bsUnescapeStr(char *str) {

    int c;
    char *in = str;
    size_t len = 0;
    bool captured = false;

    /* keep scanning */
    while((c = *in) != '\0') {

	if(c == BS_ESCAPE_CHAR) {
	    c = *++in;

	    /* this is  an escape sequence */
	    if(cclass(BF_ESS)) {
		/* place the corresponding control char */
		str[len] = esccodes[c];
		captured = true;
	    }
	}

	if(captured) {
	    captured = false;
	} else {
	    str[len] = c;
	    if(c == '\0') {
		break;
	    }
	}

	in++;
	len++;

    }

    str[len] = '\0';

    return len + 1;

}

/* place escaped string in @out, return required length (also if @out is 0) including NUL-termination */
inline size_t bsEscapeStr(const char *src, char *out) {

    int c;
    char *in = (char*)src;
    size_t len = 1;

    /* keep scanning */
    while((c = *(in++)) != '\0') {

	if(cclass(BF_ESC)) {
	    if(out != NULL) {
		*(out++) = BS_ESCAPE_CHAR;
		*(out++) = esccodes[c];
	    }
	    len += 2;
	} else if (c == BS_PATH_SEP) {
	    if(out != NULL) {
		*(out++) = BS_ESCAPE_CHAR;
		*(out++) = c;
	    }
	    len += 2;
	} else {
	    if(out != NULL) {
		*(out++) = c;
	    }
	    len++;
	}

    }

    if(out != NULL) {
	*out = '\0';
    }

    return len;

}

/* get an escaped duplicate of string src */
inline char* bsGetEscapedStr(const char* src) {

    size_t sl = bsEscapeStr(src, NULL);
    char* out;

    xmalloc(out, sl);
    bsEscapeStr(src, out);

    return out;

}

/* walk through string @in, and write to + return next token between the 'sep' character */
static inline BsToken* unescapeToken(BsToken* out, char** in, const char sep) {

    size_t ssize = BS_QUOTED_STARTSIZE;
    bool captured = false;
    int c;

    /* skip past the separator and proper whitespace */
    while( (((c = **in) == sep) || cclass(BF_WSP)) && c != '\0') {
	(*in)++;
    }

    if(c == '\0') {
	return NULL;
    }

    out->len = 0;
    xmalloc(out->data, ssize + 1);

    /* keep scanning */
    while(((c = **in) != sep) && c != '\0') {

	if(c == BS_ESCAPE_CHAR) {
	    (*in)++;
	    c = **in;
	    /* this is  an escape sequence */
	    if(cclass(BF_ESS)) {
		/* place the corresponding control char */
		out->data[out->len] = esccodes[c];
		captured = true;
	    /* but the separator can also be escaped */
	    } else if(c == sep) {
		out->data[out->len] = sep;
		captured = true;
	    }
	}

	if(captured) {
	    captured = false;
	} else {
	    if(c == '\0') {
		goto finalise;
	    }
	    out->data[out->len] = c;
	}

	(*in)++;
	out->len++;

	/* grow - we do not bother shrinking to fit, these are usually short-lived and used for hashing */
	if(out->len == ssize) {
	    ssize *= 2;
	    xrealloc(out->data, out->data, ssize + 1);
	}

    }

    if(c != '\0') {
	(*in)++;
    }

finalise:

    out->data[out->len] = '\0';
    return out;

}

/* recursive node rehash callback */
static void* bsRehashCallback(BsDict *dict, BsNode *node, void* user, void* feedback, bool* stop) {

    if(node->parent != NULL) {
	if(!(dict->flags & BS_NOINDEX)) {
	    bsIndexDelete(dict->index, node);
	}
	node->hash = BS_MIX_HASH(xxHash32(node->name, node->nameLen), node->parent->hash, node->nameLen);
	if(!(dict->flags & BS_NOINDEX)) {
	    bsIndexPut(dict, node);
	}
    }

    return NULL;
}

/* run a callback recursively on node, return node where callback stoped the walk */
BsNode* bsNodeWalk(BsDict *dict, BsNode *node, void* user, void* feedback, BsCallback callback) {

    bool stop = false;

    BsNode *n, *o;

    void* feedback1 = callback(dict, node, user, feedback, &stop);

    if(stop) {
	return node;
    }

    LL_FOREACH_DYNAMIC(node,n) {
	o = bsNodeWalk(dict, n, user, feedback1, callback);
	if(o != NULL) {
	    return o;
	}
    }

    return NULL;
}

/* run a callback recursively on dictionary, return node where callback stopped the walk */
BsNode*  bsWalk(BsDict *dict, void* user, BsCallback callback) {

    return bsNodeWalk(dict, dict->root, user, NULL, callback);

}

/* run a callback recursively on node, return linked list that callback permitted */
LList* bsNodeFilter(LList* list, BsDict *dict, BsNode *node, void* user, void *feedback, BsCallback callback) {

    bool stop = false;
    BsNode *n;

    void* feedback1 = callback(dict, node, user, feedback, &stop);

    if(list == NULL) {
	list = llCreate();
    }

    if(stop) {
	llAppendItem(list, node);
    }

    LL_FOREACH_DYNAMIC(node,n) {
	bsNodeFilter(list, dict, n, user, feedback1, callback);
    }

    return list;
}

/* run a callback recursively on dictionary, return linked list that callback permitted */
LList* bsFilter(LList* list, BsDict *dict, void* user, BsCallback callback) {

    if(list == NULL) {
	list = llCreate();
    }

    bsNodeFilter(list, dict, dict->root, user, NULL, callback);
    return list;

}

/* run a callback recursively on node, passing a BsToken with node's full path as feedback */
BsNode* bsNodePWalk(BsDict *dict, BsNode *node, void* user, void* feedback, BsCallback callback, bool escape) {

    BsToken *ptok = feedback;
    BsToken tok = { "\0", 0, 0 };
    bool stop = false;
    BsNode *n, *o;
    size_t pl = 0;

    if(ptok != NULL && ptok->data != NULL && ptok->data[0] != '\0') {
	pl = ptok->len + 1;
    } else {
	/* this way we never give the callback a NULL */
	pl = 0;
    }

    size_t sl = node->nameLen + pl;

    /*
     * twice the name len because we can potentially escape every character,
     * and we allocate on the stack, so anything in a {} scope is local,
     * and is gone once we leave the scope, so we can't do two different
     * stack allocations depending on escape contidion.
     * this whole idea is dangerous territory anyway.
     */
    char name[sl + node->nameLen + 1];

    if(pl > 1) {
	memcpy(name, ptok->data, ptok->len);
	name[ptok->len]='/';
    }
    if(sl > 0) {
	tok.data = name;
	if(escape) {
	    size_t enl = bsEscapeStr(node->name, NULL);

	    char ename[enl];
	    bsEscapeStr(node->name, ename);
	    /* this also copies the NUL termination... */
	    memcpy(name + pl, ename, enl);
	    tok.len = pl + enl - 1;
	    /* ...but we can never be too careful */
	    name[tok.len] = '\0';
	} else {
	    memcpy(name + pl, node->name, node->nameLen);
	    name[sl] = '\0';
	    tok.len = sl;
	}
    }

    /* callback uses the path */
    callback(dict, node, user, &tok, &stop);

    if(stop) {
	return node;
    }

    LL_FOREACH_DYNAMIC(node,n) {
	/* recursion expands the path */
	o = bsNodePWalk(dict, n, user, &tok, callback, escape);
	if(o != NULL) {
	    return o;
	}
    }

    return NULL;
}

/* same as bsWalk, but every callback is passed a BsToken as feedback, with node's full path */
BsNode*  bsPWalk(BsDict *dict, void* user, BsCallback callback, bool escape) {

    return bsNodePWalk(dict, dict->root, user, NULL, callback, escape);
}

/*
 * run a callback recursively on node, return linked list of nodes
 * that callback permitted, callback gets node path. If NULL passed as list,
 * create one, if not - append.
 */
LList* bsNodePFilter(LList *list, BsDict *dict, BsNode *node, void* user, void *feedback, BsCallback callback, bool escape) {

    BsToken *ptok = feedback;
    BsToken tok = { "\0", 0, 0 };
    bool stop = false;
    BsNode *n;
    size_t pl = 0;

    if(ptok != NULL && ptok->data != NULL && ptok->data[0] != '\0') {
	pl = ptok->len + 1;
    } else {
	/* this way we never give the callback a NULL */
	pl = 0;
    }

    size_t sl = node->nameLen + pl;

    /*
     * twice the name len because we can potentially escape every character,
     * and we allocate on the stack, so anything in a {} scope is local,
     * and is gone once we leave the scope, so we can't do two different
     * stack allocations depending on escape contidion.
     * this whole idea is dangerous territory anyway.
     */
    char name[sl + node->nameLen + 1];

    if(pl > 1) {
	memcpy(name, ptok->data, ptok->len);
	name[ptok->len]='/';
    }

    if(sl > 0) {
	tok.data = name;
	if(escape) {
	    size_t enl = bsEscapeStr(node->name, NULL);

	    char ename[enl];
	    bsEscapeStr(node->name, ename);
	    /* this also copies the NUL termination... */
	    memcpy(name + pl, ename, enl);
	    tok.len = pl + enl - 1;
	    /* ...but we can never be too careful */
	    name[tok.len] = '\0';
	} else {
	    memcpy(name + pl, node->name, node->nameLen);
	    name[sl] = '\0';
	    tok.len = sl;
	}
    }

    /* callback uses the path */
    callback(dict, node, user, &tok, &stop);

    if(list == NULL) {
	list = llCreate();
    }

    if(stop) {
	llAppendItem(list, node);
    }

    LL_FOREACH_DYNAMIC(node,n) {
	/* recursion expands the path */
	bsNodePFilter(list, dict, n, user, &tok, callback, escape);
    }

    return list;

}

/* run a callback recursively on dictionary, return linked list that callback permitted, callback gets node path */
LList* bsPFilter(LList* list, BsDict *dict, void* user, BsCallback callback, bool escape) {

    if(list == NULL) {
	list = llCreate();
    }

    bsNodePFilter(list, dict, dict->root, user, NULL, callback, escape);

    return list;

}

/* callback for use with bsFilter, checking if node value contains string */
void* bsValueContainsCb(BsDict *dict, BsNode *node, void* user, void* feedback, bool* matches) {

    if(node->value != NULL && user != NULL && strstr(node->value, user)) {
	*matches = true;
    }

    return NULL;

}

/* callback for use with bsFilter, checking if node value contains string */
void* bsNameContainsCb(BsDict *dict, BsNode *node, void* user, void* feedback, bool* matches) {

    if(node->name != NULL && user != NULL && strstr(node->name, user)) {
	*matches = true;
    }

    return NULL;

}

/* print error hint from state structure */
static void bsErrorHint(BsState *state) {

	size_t lw = BS_ERRORDUMP_LINEWIDTH;
	size_t hlw = lw / 2;
	char *marker = state->linestart;
	char linebuf[lw + 1];
	char pointbuf[lw + 1];
	bool rtrunc = true;


	memset(pointbuf, ' ', lw);
	memset(linebuf, 0, lw + 1);
	pointbuf[lw] = '\0';

	if(state->linepos > hlw) {
	    marker += state->linepos - hlw;
	    pointbuf[hlw] = '^';
	} else {
	    pointbuf[state->linepos] = '^';
	}

	for(int i = 0; i < lw ; i++) {
	    if((*marker == '\0') || chclass(*marker, BF_NLN)) {
		rtrunc = false;
		break;
	    }
	    linebuf[i] = *(marker++);
	}

	fprintf(stderr,"\t%s%s%s\n\t%s%s\n",
	    state->linepos > hlw ? "..." : "", linebuf,
	    rtrunc ? "..." : "",
	    state->linepos > hlw ? "   " : "", pointbuf);

}

/* print parser error - must be done before the source buffer is freed */
void bsPrintError(BsState *state) {

    if(state->parseError == BS_PERROR_NONE) {

	fprintf(stderr, "No error: parsed successfully\n");
	return;

    } else {

	fprintf(stderr, "Parse error: ");
	switch (state->parseError) {
	    case BS_PERROR_EOF:

		switch(state->scanState) {
		    case BS_GET_QUOTED:
			fprintf(stderr, "Unterminated quoted string");
			restorestate(state);
			break;
		    case BS_SKIP_MLCOMMENT:
			fprintf(stderr, "Unterminated multiline comment");
			restorestate(state);
			break;
		    default:
			fprintf(stderr, "Unexpected EOF");
			break;
		}

		break;
	    case BS_PERROR_UNEXPECTED:
		fprintf(stderr, "Unexpected character: '%c' (0x%02x)", *state->current, *state->current);
		break;
	    case BS_PERROR_LEVEL:
		restorestate(state);
		fprintf(stderr, "Unbalanced bracket(s) found");
		break;
	    case BS_PERROR_TOKENS:
		fprintf(stderr, "Too many consecutive identifiers");
		break;
	    case BS_PERROR_EXP_ID:
		restorestate(state);
		fprintf(stderr, "Expected node name / identifier");
		break;
	    case BS_PERROR_UNEXP_ID:
		fprintf(stderr, "Unexpected node name / identifier");
		break;
	    case BS_PERROR_BLOCK:
		fprintf(stderr, "Unexpected block element");
		break;
	    case BS_PERROR_NULL:
		fprintf(stderr, "Dictionary object is NULL\n");
		return;
	    case BS_PERROR_QUOTED:
		restorestate(state);
		fprintf(stderr, "Unterminated quoted string");
		break;
	    default:
		fprintf(stderr, "Unexpected parser error 0x%x\n", state->parseError);
	}

	fprintf(stderr," at line %zd position %zd:\n\n", state->lineno, state->linepos + 1);
	bsErrorHint(state);

    }

}

/* main buffer scanner / lexer state machine */
static inline void bsScan(BsState *state) {

    size_t ssize;
    int qchar = BS_QUOTE_CHAR;
    int c = *state->current;
    BsToken *tok = &state->tokenCache[state->tokenCount];

    /*
     * continue scanning until an event occurs. note that break; is crucial
     * in some places because we need to check for control characters.
     */
    do {
	/* search for tokens or quoted strings, skip comments, etc. */
	again: /* short-circuit */
	switch (state->scanState) {

	    case BS_SKIP_WHITESPACE:
		while(cclass(BF_SPC | BF_NLN)) {
		    c = bsForward(state);
		}
		/* we have reached a multiline comment outer character... */
		if(c == BS_MLCOMMENT_OUT_CHAR) {
		    /* ...and the next character is the inner multiline comment character */
		    if(bsPeek(state) == BS_MLCOMMENT_IN_CHAR) {
			/* save state when entering a 'find closing character' type state */
			savestate(state);
			c = bsForward(state);
			state->scanState = BS_SKIP_MLCOMMENT;
			goto again;
		    }
		    /* ...and the next character is also a multiline comment outer character */
		    if(bsPeek(state) == BS_MLCOMMENT_OUT_CHAR) {
			c = bsForward(state);
			state->scanState = BS_SKIP_COMMENT;
			goto again;
		    }
		}
		state->scanState = BS_GET_TOKEN;
		break;

	    case BS_GET_TOKEN:
		tok->data = state->current;
		tok->len = 0;
		tok->quoted = 0;
		/*
		 * ...and we have a problem. Juniper uses ':' in both names and in values,
		 * so if we want to parse JSON, we have a conflict, because of the ':' value separator.
		 */
//		int flags = BF_TOK | (!!state->tokenCount * BF_EXT);
		while(cclass(BF_TOK | BF_EXT)) {
			c = bsForward(state);
			tok->len++;
		}

		/* raise a "got token" event if we got anything */
		if(tok->len > 0) {
		    state->scanState = BS_SKIP_WHITESPACE;
		    state->parseEvent = BS_GOT_TOKEN;
		    return;
		}

		break;

	    case BS_GET_QUOTED:
		ssize = BS_QUOTED_STARTSIZE;
		tok->len = 0;
		tok->quoted = ~0;
		xmalloc(tok->data, ssize + 1);
		bool captured;

	        nextbatch:

		while(c != qchar) {

		    if(cclass(BF_NLN)) {
			    state->parseEvent = BS_ERROR;
			    state->parseError = BS_PERROR_QUOTED;
			    return;
		    }

		    if(c == BS_ESCAPE_CHAR) {
			c = bsForward(state);
			/* this is  an escape sequence */
			if(cclass(BF_ESS)) {
			    /* place the corresponding control char */
			    tok->data[tok->len] = esccodes[c];
			    captured = true;
			}
		    }

		    if(captured) {
			captured = false;
		    } else {
			if(c == EOF) {
			    state->parseEvent = BS_ERROR;
			    state->parseError = BS_PERROR_EOF;
			    return;
			}
			tok->data[tok->len] = c;
		    }
		    c = bsForward(state);
		    tok->len++;
		    if(tok->len == ssize) {
			ssize *= 2;
			xrealloc(tok->data, tok->data, ssize + 1);
		    }
		}
		
		c = bsForward(state);

		/* try a multiline string */
		if(c == BS_ESCAPE_CHAR) {

		    /* skip whitespaces and newlines */
		    do {
			savestate(state);
			c = bsForward(state);
		    } while(cclass(BF_WSP | BF_NLN));

		    /* continue consuming multiline string */
		    if(c == qchar) {
			c = bsForward(state);
			goto nextbatch;
		    } else {
			state->parseEvent = BS_ERROR;
			state->parseError = BS_PERROR_QUOTED;
			return;
		    }
		}

		tok->data[tok->len] = '\0';

		/* raise a "got token" event */
		state->parseEvent = BS_GOT_TOKEN;
		state->scanState = BS_SKIP_WHITESPACE;
		return;

	    case BS_SKIP_COMMENT:
		while(!cclass(BF_NLN)) {
		    c = bsForward(state);
		}
		state->scanState = BS_SKIP_NEWLINE;
		/* fall-through */

	    case BS_SKIP_NEWLINE:
		while(cclass(BF_NLN)) {
		    c = bsForward(state);
		}
		state->scanState = BS_SKIP_WHITESPACE;
		break;

	    case BS_SKIP_MLCOMMENT:

		while(c != BS_MLCOMMENT_OUT_CHAR && c != EOF) {
		    c = bsForward(state);
		}
		if(c == BS_MLCOMMENT_OUT_CHAR) {
		    if (state->prev == BS_MLCOMMENT_IN_CHAR) {
			/* end of comment */
			state->scanState = BS_SKIP_WHITESPACE;
		    }
		    /* keep moving on */
		    c = bsForward(state);
		} else if (c == EOF) {
		    state->parseEvent = BS_ERROR;
		    state->parseError = BS_PERROR_EOF;
		    return;
		}
		break;

	    default:
		state->parseEvent = BS_ERROR;
		state->parseError = BS_PERROR;
		return;

	}

	/* if no event raised, check for control characters, raise parser events and move search state accordingly */
	if(state->parseEvent == BS_NOEVENT) {

	    switch(c) {
#ifdef BS_QUOTE1_CHAR
		case BS_QUOTE1_CHAR:
#endif
#ifdef BS_QUOTE2_CHAR
		case BS_QUOTE2_CHAR:
#endif
#ifdef BS_QUOTE3_CHAR
		case BS_QUOTE3_CHAR:
#endif
		case BS_QUOTE_CHAR:
		    state->scanState = BS_GET_QUOTED;
		    /* save state when entering a 'find closing character' type state */
		    savestate(state);
		    /* because we can have two quotation mark types, we save the one we encountered */
		    qchar = c;
		    c = bsForward(state);
		    goto again;
#ifdef BS_ENDVAL1_CHAR
		case BS_ENDVAL1_CHAR:
#endif
#ifdef BS_ENDVAL2_CHAR
		case BS_ENDVAL2_CHAR:
#endif
#ifdef BS_ENDVAL3_CHAR
		case BS_ENDVAL3_CHAR:
#endif
#ifdef BS_ENDVAL4_CHAR
		case BS_ENDVAL4_CHAR:
#endif
#ifdef BS_ENDVAL5_CHAR
		case BS_ENDVAL5_CHAR:
#endif
		case BS_ENDVAL_CHAR:
		    state->scanState = BS_SKIP_WHITESPACE;
		    state->parseEvent = BS_GOT_ENDVAL;
		    bsForward(state);
		    return;
		/*
		 * note: we could minimise the number of cases if we defined event type constants
		 * as the corresponding characters. So if BS_GOT_BLOCK = BS_STARTBLOCK_CHAR,
		 * we just have parseEvent = c. This could be used for a fast JSON parser.
		 */
		case BS_STARTBLOCK_CHAR:
		    savestate(state);
		    state->scanState = BS_SKIP_WHITESPACE;
		    state->parseEvent = BS_GOT_BLOCK;
		    bsForward(state);
		    return;
		case BS_ENDBLOCK_CHAR:
		    state->scanState = BS_SKIP_WHITESPACE;
		    state->parseEvent = BS_END_BLOCK;
		    bsForward(state);
		    return;
		case BS_STARTARRAY_CHAR:
		    savestate(state);
		    state->scanState = BS_SKIP_WHITESPACE;
		    state->parseEvent = BS_GOT_ARRAY;
		    bsForward(state);
		    return;
		case BS_ENDARRAY_CHAR:
		    state->scanState = BS_SKIP_WHITESPACE;
		    state->parseEvent = BS_END_ARRAY;
		    bsForward(state);
		    return;
		case BS_COMMENT_CHAR:
		    state->scanState = BS_SKIP_COMMENT;
		    c = bsForward(state);
		    goto again;
		/* bsForward() _currently_ returns EOF on end, but we still need to handle NUL */
		case '\0':
		case EOF:
		    /* this is a 'legal' end of buffer, as opposed to unexpected EOF */
		    state->parseEvent = BS_GOT_EOF;
		    return;
		default:
		    if(cclass(BF_ILL)) {
			state->parseEvent = BS_ERROR;
			state->parseError = BS_PERROR_UNEXPECTED;
			return;
		    }
		break;
	    }
	}

    } while(state->parseEvent == BS_NOEVENT);

    return;
}

/*
 * parse the contents of buf into dictionary dict, return last state.
 * a lot of this logic (different token number cases) is to allow consumption
 * of weirder formats like Juniper configuration. There should be a simplified
 * version for JSON which has none of that, and a "native" one that forgoes
 * some of the Juniper oddness.
 */
BsState bsParse(BsDict *dict, char *buf, const size_t len) {

    /* node stack, so we can return n levels up if we created multiple in one go */
    PST_DECL(nodestack, BsNode*, 16);

    BsNode *head;
    BsNode *newnode;
    BsState state;

    bsInitState(&state, buf, len);

    if(dict == NULL) {
	state.parseError = BS_PERROR_NULL;
	return state;
    }

    head = dict->root; /* this is the current node we are appending to */
    PST_INIT(nodestack);

    /* keep parsing until no more data or parser error encountered */
    while(!state.parseError) {

	state.parseEvent = BS_NOEVENT;
	state.parseError = BS_PERROR_NONE;

	/* scan state machine runs until it barfs an event */
	bsScan(&state);

	/* process parser event */
	switch(state.parseEvent) {

	    /* we got a token or quoted string - increment counter and check if we can handle the count */
	    case BS_GOT_TOKEN:

		/* check for node modifiers */
		if(state.tokenCount == 0 && state.prev == BS_MODIFIER_CHAR) {
		    /* "inactive" modifier */
		    if(!strncmp(ts(0), "inactive", tl(0) - 1)) {
			state.flags |= BS_INACTIVE;
			state.tokenOffset++;
		    }
		}

		if(++state.tokenCount == BS_MAX_TOKENS) {
		    /* we can have as many tokens as we want when in an array, add them in batches */
		    if(head->type == BS_NODE_ARRAY) {
			for(int i = state.tokenOffset; i < state.tokenCount; i++) {
			    newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, NULL, 0, td(i), tl(i));
			    newnode->flags |= BS_QUOTED_VALUE & tq(i);
			}
			tokenreset();
		    } else{
			state.parseEvent = BS_ERROR;
			state.parseError = BS_PERROR_TOKENS;
		    }
		}
		break;

	    /* we got start of a block, i.e. '{' */
	    case BS_GOT_BLOCK:

		/*
		 * create different node arrangements based on token count,
		 * note that we push the current parent / head onto the stack,
		 * so that we can return to it when this block ends, rather than returning
		 * upwards to some nested node that is obviously not it.
		 */

		/* it's all different for arrays, because ' a b c { something }' means 4 nodes - 3 leaves and a branch member */
		if(head->type == BS_NODE_ARRAY) {

		    /* first insert any existing tokens as array leaves */
		    for(int i = state.tokenOffset; i < state.tokenCount; i++) {
			newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, NULL, 0, td(i), tl(i));
			newnode->flags |= BS_QUOTED_VALUE & tq(i);
			newnode->flags |= state.flags;
		    }

		    /* now enter into an unnamed branch which is a new member of the array */
		    PST_PUSH_GROW(nodestack, head); /* save current position */
		    newnode = _bsCreateNode(dict, head, BS_NODE_BRANCH, NULL, 0, NULL, 0);
		    head = newnode;

		} else {
		    switch(state.tokenCount - state.tokenOffset) {
			case 1:
			    PST_PUSH_GROW(nodestack, head);
			    /*
			     * the macros td, tq and tl are defined at the top of this file. They simply
			     * grab the data, quoted field and len field from the given item in token cache.
			     */
			    newnode = _bsCreateNode(dict, head, BS_NODE_BRANCH, td(0), tl(0), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(0);
			    newnode->flags |= state.flags;
			    head = newnode;
			    break;
			case 2:
			    PST_PUSH_GROW(nodestack, head);
			    newnode = _bsCreateNode(dict, head, BS_NODE_INSTANCE, td(0), tl(0), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(0);
			    newnode->flags |= state.flags;
			    newnode = _bsCreateNode(dict, newnode, BS_NODE_BRANCH, td(1), tl(1), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(1);
			    head = newnode;
			    break;
			case 3:
			    /* or should we swap instance and branch - compare with JunOS */
			    PST_PUSH_GROW(nodestack, head);
			    newnode = _bsCreateNode(dict, head, BS_NODE_INSTANCE, td(0), tl(0), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(0);
			    newnode->flags |= state.flags;
			    newnode = _bsCreateNode(dict, newnode, BS_NODE_BRANCH, td(1), tl(1), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(1);
			    newnode = _bsCreateNode(dict, newnode, BS_NODE_BRANCH, td(2), tl(2), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(2);
			    head = newnode;			
			    break;
			/* unnamed branch? only at root level and only once */
			case 0:
			    if(state.tokenCount == 0 && head == dict->root && nodestack_sh == 0) {
				/* imaginary descent. This allows us to put empty {}s around the whole content */
				PST_PUSH_GROW(nodestack, head);
			    /* nope. */
			    } else {
				state.parseEvent = BS_ERROR;
				state.parseError = BS_PERROR_EXP_ID;
			    }
			    break;

			default:
			    break;
		    }
		}
		/* we don't need the tokens anymore */
		tokenreset();
		break;

	    /*
	     * end of block. remaining tokens do not need to be terminated,
	     * so if we have any, we fall through to endval case. If we have none,
	     * pop the last branching point off the stack and we're golden.
	     * without the fall through, this would have to be a duplicate of the endval case.
	     */
	    case BS_END_BLOCK:

		if(state.tokenCount == 0) {
		    /* 
		     * WOOP WOOP, WIND SHEAR, BANK ANGLE, PULL UP, TOO LOW, TERRAIN, TERRAIN
		     * we cannot move up the node stack since we are already at the top
		     */
		    if(PST_EMPTY(nodestack)) {
			state.parseEvent = BS_ERROR;
			state.parseError = BS_PERROR_BLOCK;
			break;
		    }
		    head = PST_POP(nodestack);
		    break;
		}

		/* end of block inside an array? nope. */
		if(head->type == BS_NODE_ARRAY) {
		    state.parseEvent = BS_ERROR;
		    state.parseError = BS_PERROR_BLOCK;
		    break;
		}

		/* fall-through to GOT_ENDVAL */
	    
	    /* we encountered an end of value indication like ';' or ',' (JSON) */
	    case BS_GOT_ENDVAL:

		/* arrays are special that way, a single token is a leaf with a value */
		if(head->type == BS_NODE_ARRAY) {

		    switch(state.tokenCount - state.tokenOffset) {

			case 1:
			    newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, NULL, 0, NULL, 0);
			    newnode->flags |= state.flags;
			    newnode->value = td(0);
			    newnode->valueLen = tl(0);
			    newnode->flags |= BS_QUOTED_VALUE & tq(0);
			    break;
			/* this is only a courtesy thing. array members are always unnamed - we only take the value */
			case 2:
			    newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, NULL, 0, td(1), tl(1));
			    newnode->flags |= BS_QUOTED_VALUE & tq(1);
			    newnode->flags |= state.flags;
			    break;
			/* stray endval character, ignore */
			case 0:
			    break;
			default:
			    state.parseEvent = BS_ERROR;
			    state.parseError = BS_PERROR_TOKENS;
			break;
		    }

		} else {

		    switch(state.tokenCount - state.tokenOffset) {

			case 1:
			    newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, td(0), tl(0), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(0);
			    newnode->flags |= state.flags;
			    break;
			case 2:
			    newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, td(0), tl(0), td(1), tl(1));
			    newnode->flags |= BS_QUOTED_NAME & tq(0);
			    newnode->flags |= BS_QUOTED_VALUE & tq(1);
			    newnode->flags |= state.flags;
			    break;
			case 3:
			    newnode = _bsCreateNode(dict, head, BS_NODE_INSTANCE, td(0), tl(0), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(0);
			    newnode->flags |= state.flags;
			    newnode = _bsCreateNode(dict, newnode, BS_NODE_BRANCH, td(1), tl(1), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(1);
			    newnode = _bsCreateNode(dict, newnode, BS_NODE_LEAF, td(2), tl(2), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(2);
			    break;
			case 4:
			    newnode = _bsCreateNode(dict, head, BS_NODE_INSTANCE, td(0), tl(0), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(0);
			    newnode->flags |= state.flags;
			    newnode = _bsCreateNode(dict, newnode, BS_NODE_BRANCH, td(1), tl(1), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(1);
			    newnode = _bsCreateNode(dict, newnode, BS_NODE_LEAF, td(2), tl(2), td(3), tl(3));
			    newnode->flags |= BS_QUOTED_NAME & tq(2);
			    newnode->flags |= BS_QUOTED_VALUE & tq(3);
			    break;
			/* stray endval character, ignore */
			case 0:
			    break;

			/* at least 5 tokens */
			default:

			    /* too many tokens */
			    if(state.tokenCount > BS_MAX_TOKENS) {

				state.parseEvent = BS_ERROR;
				state.parseError = BS_PERROR_TOKENS;

			    } else {

				/*
				* 5+ consecutive tokens we treat as branch with (n-1) / 2 leaf-value pairs,
				* if the number is odd, the last leaf has no value.
				*/
				newnode = _bsCreateNode(dict, head, BS_NODE_BRANCH, td(0), tl(0), NULL, 0);
				newnode->flags |= BS_QUOTED_NAME & tq(0);
				newnode->flags |= state.flags;

				BsNode *tmphead = newnode;

				for(int i = state.tokenOffset + 1; i < state.tokenCount; i++) {

				    if((i + 1) < state.tokenCount) {
					newnode = _bsCreateNode(dict, tmphead, BS_NODE_LEAF, td(i), tl(i), td(i+1), tl(i+1));
					newnode->flags |= BS_QUOTED_NAME & tq(i);
					newnode->flags |= BS_QUOTED_VALUE & tq(i+1);
					i++;
				    } else {
					newnode = _bsCreateNode(dict, tmphead, BS_NODE_LEAF, td(i), tl(i), NULL, 0);
					newnode->flags |= BS_QUOTED_NAME & tq(i);
				    }
				}

			    }

			    break;
		    }
		}

		/* aftermath of the previous fall-through */
		if(state.parseEvent == BS_END_BLOCK) {
		    /* 
		     * WOOP WOOP, WIND SHEAR, BANK ANGLE, PULL UP, TOO LOW, TERRAIN, TERRAIN
		     * we cannot move up the node stack since we are already at the top
		     */
		    if(PST_EMPTY(nodestack)) {
			state.parseEvent = BS_ERROR;
			state.parseError = BS_PERROR_LEVEL;
			break;
		    }
		    head = PST_POP(nodestack);
		}

		tokenreset();
		break;

	    /* array start block i.e. '[' */
	    case BS_GOT_ARRAY:

		/* handle nested arrays - same case as GOT_BLOCK in an array */
		if(head->type == BS_NODE_ARRAY) {

		    /* first insert any existing tokens as array leaves */
		    for(int i = state.tokenOffset; i < state.tokenCount; i++) {
			newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, NULL, 0, td(i), tl(i));
			newnode->flags |= BS_QUOTED_VALUE & tq(i);
		    }

		    /* now enter into an unnamed array which is a new member of the upper array */
		    PST_PUSH_GROW(nodestack, head); /* save current position */
		    newnode = _bsCreateNode(dict, head, BS_NODE_ARRAY, NULL,0, NULL, 0);
		    head = newnode;

		} else {

		    switch(state.tokenCount - state.tokenOffset) {
			case 1:
			    PST_PUSH_GROW(nodestack, head);
			    newnode = _bsCreateNode(dict, head, BS_NODE_ARRAY, td(0), tl(0), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(0);
			    newnode->flags |= state.flags;
			    head = newnode;
			    break;
			case 2:
			    PST_PUSH_GROW(nodestack, head);
			    newnode = _bsCreateNode(dict, head, BS_NODE_INSTANCE, td(0), tl(0), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(0);
			    newnode->flags |= state.flags;
			    newnode = _bsCreateNode(dict, newnode, BS_NODE_ARRAY, td(1), tl(1), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(1);
			    head = newnode;
			    break;
			case 3:
			    PST_PUSH_GROW(nodestack, head);
			    newnode = _bsCreateNode(dict, head, BS_NODE_INSTANCE, td(0), tl(0), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(0);
			    newnode->flags |= state.flags;
			    newnode = _bsCreateNode(dict, newnode, BS_NODE_BRANCH, td(1), tl(1), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(1);
			    newnode = _bsCreateNode(dict, newnode, BS_NODE_ARRAY, td(2), tl(2), NULL, 0);
			    newnode->flags |= BS_QUOTED_NAME & tq(2);
			    head = newnode;
			    break;
			/* unnamed array?  nope. */
			case 0:
			    state.parseEvent = BS_ERROR;
			    state.parseError = BS_PERROR_EXP_ID;
			    break;
			default:
			    break;
		    }

		}

		tokenreset();

		break;

	    /* array end block i.e. ']' */
	    case BS_END_ARRAY:

		/* end of arRAY OUTSIDE AN ARRAY?! What are you, some kind of an animal?! */
		if(head->type != BS_NODE_ARRAY) {
		    state.parseEvent = BS_ERROR;
		    state.parseError = BS_PERROR_BLOCK;
		}

		/*
		 * We allow some flexibility when constructing arrays. If we reach the end of an array,
		 * any leftover tokens are added as array leaves. This means that an array can be defined
		 * as a list of whitespace-separated tokens.
		 */
		for(int i = state.tokenOffset; i < state.tokenCount; i++) {
		    newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, NULL, 0, td(i), tl(i));
		    newnode->flags |= BS_QUOTED_VALUE & tq(i);
		}

		/* 
		 * WOOP WOOP, WIND SHEAR, BANK ANGLE, PULL UP, TOO LOW, TERRAIN, TERRAIN
		 * we cannot move up the node stack since we are already at the top...
		 * although in this particular case this should not really happen.
		 */
		if(PST_EMPTY(nodestack)) {
		    state.parseEvent = BS_ERROR;
		    state.parseError = BS_PERROR_BLOCK;
		    break;
		}

		/* return to last branching point */
		head = PST_POP(nodestack);

		tokenreset();

		break;

	    case BS_GOT_EOF:
		/* we got an EOF but were left with some tokens. */
		if(state.tokenCount > 0) {
		    state.parseEvent = BS_ERROR;
		    state.parseError = BS_PERROR_EOF;
		}
		/* all she wrote */
		goto done;
	    case BS_NOEVENT:
	    case BS_ERROR:
	    default:
		break;
	}

    }

done:

    /* we should have ended back at the root, if not, we probably have unbalanced brackets */
    if(state.parseEvent != BS_ERROR && head != dict->root) {
	state.parseEvent = BS_ERROR;
	state.parseError = BS_PERROR_LEVEL;
    }

    /* clean up */
    tokencleanup();
    PST_FREE(nodestack);

    /* return a copy of the state value so we can check for errors */
    return state;

}

/* node indexing callback - used when indexing a previously unindexed dictionary */
static void* bsIndexCallback(BsDict *dict, BsNode *node, void* user, void* feedback, bool* stop) {

    if(node->parent != NULL && !(node->flags & BS_INDEXED)) {

	bsIndexPut(dict, node);

    }

    return NULL;

}

/* node reindexing callback - used when forcing a reindex */
static void* bsReindexCallback(BsDict *dict, BsNode *node, void* user, void* feedback, bool* stop) {

    if(node->parent != NULL) {

	if (node->flags & BS_INDEXED) {
	    bsIndexDelete(dict->index, node);
	}

	bsIndexPut(dict, node);

    }

    return NULL;

}

/* index all unindexed nodes and enable indexing */
void bsIndex(BsDict* dict) {

    if(dict != NULL) {

	/* clear BS_NOINDEX flag */
	if(dict->flags & BS_NOINDEX) {
	    if(dict->index == NULL) {
		dict->index = bsIndexCreate();
	    }
	    dict->flags &= ~BS_NOINDEX;
	}

	bsWalk(dict, NULL, bsIndexCallback);

    }

}

/* force full reindex - but not a full rehash */
void bsReindex(BsDict *dict) {

    if(dict != NULL) {

	if(!(dict->flags & BS_NOINDEX)) {
	    bsWalk(dict, NULL, bsReindexCallback);	    
	}

    }
}

/*
 * Put BS_PATH_SEP-separated path of given node into out. If out is NULL,
 * required string lenth (including zero-termination) is returned and no
 * extraction is done. Otherwise the length should be passed as @outlen,
 * and path is copied into *out.
 */
size_t bsGetPath(BsNode *node, char* out, const size_t outlen) {

    size_t pathlen = 1;
    char* target = out + outlen - 1;

    if(node == NULL) {
	return 1;
    }

    for(BsNode *walker = node; walker->parent != NULL; walker = walker->parent) {

	pathlen += walker->nameLen;
	if(walker->parent->parent != NULL) {
	    pathlen++;
	}

#if 0
	if(walker->type == BS_NODE_INSTANCE) {
	    pathlen += walker->valueLen;
	    pathlen++;
	}
#endif
	if(out != NULL) {


#if 0

	    if(walker->type == BS_NODE_INSTANCE) {
		target -= walker->valueLen;
		memcpy(target, walker->value, walker->valueLen);
		target--;
		*target = BS_PATH_SEP;
	    }
#endif
	    target -= walker->nameLen;
	    memcpy(target, walker->name, walker->nameLen);
	    if(walker->parent->parent != NULL) {
		target--;
		*target = BS_PATH_SEP;
	    }
	}
    }

    if(out != NULL) {
	out[pathlen - 1] = '\0';
    }

    return pathlen;

}

/* escaped version of bsGetPath */
size_t bsGetEscapedPath(BsNode *node, char* out, const size_t outlen) {

    size_t pathlen = 1;
    char* target = out + outlen - 1;

    if(node == NULL) {
	return 1;
    }

    for(BsNode *walker = node; walker->parent != NULL; walker = walker->parent) {

	size_t elen = bsEscapeStr(walker->name, NULL) - 1;
	char ename[elen + 1];
	bsEscapeStr(walker->name, ename);

	pathlen += elen;

	if(walker->parent->parent != NULL) {
	    pathlen++;
	}

#if 0
	if(walker->type == BS_NODE_INSTANCE) {

	    size_t evlen = bsEscapeStr(walker->value, NULL) - 1;
	    char evalue[evlen + 1];
	    bsEscapeStr(walker->value, evalue);

	    pathlen += evlen;
	    pathlen++;

	    if(out != NULL) {
		target -= evlen;
		memcpy(target, evalue, evlen);
		target--;
		*target = BS_PATH_SEP;
	    }

	}
#endif
	if(out != NULL) {
	    target -= elen;
	    memcpy(target, ename, elen);
	    if(walker->parent->parent != NULL) {
		target--;
		*target = BS_PATH_SEP;
	    }
	}
    }

    if(out != NULL) {
	out[pathlen - 1] = '\0';
    }

    return pathlen;

}

/* expand escape sequences and produce a clean query trimmed on both ends, matching the bsGetPath output */
static inline size_t cleanupQuery(char* query) {

    size_t slen = 0;
    char *markerin = query;
    char *markerout = query;
    BsToken tok;

    if(query == NULL) {
	return 0;
    }

    /* iterate through BS_PATH_SEP separated tokens and place them back into the original string, cleaned up */
    while(unescapeToken(&tok, &markerin, BS_PATH_SEP)) {
	if(tok.len > 0) {
	    if(slen > 0) {
		*(markerout++) = BS_PATH_SEP;
		slen++;
	    }
	   memcpy(markerout, tok.data, tok.len);
	    markerout += tok.len;
	    slen += tok.len;
	    free(tok.data);
	}
    }

    /* terminate */
    *markerout = '\0';
    /* and return new length */
    return slen;
}

/* return a new string containing cleaned up query */
static inline char* getCleanQuery(const char* query) {

    char* cqry;
    size_t sl;

    if(query == NULL || (sl = strlen(query)) == 0) {
	xmalloc(cqry, 1);
	cqry[0] = '\0';
	return cqry;
    }

    xmalloc(cqry, sl + 1);

    memcpy(cqry, query, sl);
    cqry[sl] = '\0';
    cleanupQuery(cqry);
    return cqry;

}

/* compute the compound hash of a query path rooted ad node @root */
static inline uint32_t bsGetPathHash(BsNode* root, const char* query) {

    uint32_t hash;
    BsToken tok;
    char* marker = (char*) query;

    if(root == NULL || query == NULL) {
	return 0;
    }

    hash = root->hash;

    /* iterate over tokens */
    while(unescapeToken(&tok, &marker, BS_PATH_SEP)) {
	hash = BS_MIX_HASH(xxHash32(tok.data, tok.len), hash, tok.len);
	free(tok.data);
    }

    return hash;

}

/* find a single / last descendant of node based on path, and verify that path matches */
BsNode* bsNodeGet(BsDict* dict, BsNode *node, const char* qry) {

    BsToken tok;
    uint32_t hash;
    BsNode *n = NULL;
    char *cqry;
    char *marker;

    if(qry != NULL) {

	hash = bsGetPathHash(node, qry);
        cqry = getCleanQuery(qry);

	if(cqry != NULL) {

	    /* if the dictionary is indexed, search in index */
	    if(!(dict->flags & BS_NOINDEX)) {

		for(n = bsIndexGet(dict->index, hash); n != NULL; n = n->_indexNext) {
		    BS_GETNP(n, path);
		    /* getCleanQuery() always produces either a null-terminated string or NULL */
		    if(!strcmp(cqry, path)) {
		        free(cqry);
		        return n;
		    }
		}
	    /* otherwise do a naive search */
	    } else {

		LList* l = llCreate();
		/* we start with the parent node */
		llAppendItem(l, node);
		LListMember *mb;
		LList* m = NULL;

		marker = (char*)qry;

		/* iterate over tokens, moving down the tree as we find children token by token */
		while((l->count > 0) && unescapeToken(&tok, &marker, BS_PATH_SEP)) {

		     /* iterate over all children matching path so far*/
		    LL_FOREACH_DYNAMIC(l, mb) {

			 /* append all children matching current token */
			 m = _bsGetChildren(m, dict, mb->value, tok.data, tok.len);

		    }

		    /* drop the original list */
		    llFree(l);

		    /* we will now iterate over the deeper list */
		    l = m;
		    m = NULL;		    

		    free(tok.data);

		}

		free(cqry);

		if(l->count > 0) {
		    n = l->_firstChild->value;
		}

		llFree(l);
		return n;

	    }

	    free(cqry);

	}

    }

    return NULL;

}

/* only a shortcut to query the root of the dictionary */
BsNode* bsGet(BsDict* dict, const char* qry) {

    return bsNodeGet(dict, dict->root, qry);
}

/* public version that calls strlen */
BsNode* bsGetChild(BsDict* dict, BsNode *parent, const char* name) {

    if(name == NULL) {
	return NULL;
    }

    return _bsGetChild(dict, parent, name, strlen(name));
}

/* public version that calls strlen */
LList* bsGetChildren(LList* out, BsDict* dict, BsNode *parent, const char* name) {

    if(name == NULL) {
	return NULL;
    }

    return _bsGetChildren(out, dict, parent, name, strlen(name));
}


/*
 * Get parent's n-th child - simple iterative search. Yes, we could have
 * a separate index keyed on child numbers, but do we want that?
 */
BsNode* bsNthChild(BsDict* dict, BsNode *parent, const unsigned int childno) {

    unsigned int i = 0;
    BsNode* n;

    if(parent == NULL || parent->childCount == 0) {
	return NULL;
    }

    if(childno >= parent->childCount) {
	return NULL;
    }

    /* children are a doubly linked list, so if we are above half, count from the end */
    if(childno > (parent->childCount / 2)) {

	i = parent->childCount - 1;

	LL_FOREACH_DYNAMIC_REVERSE(parent, n) {

	    if(i == childno) {
		return n;
	    }
	    i--;

	}
    /* otherwise count from beginning */
    } else {

	LL_FOREACH_DYNAMIC(parent, n) {

	    if(i == childno) {
		return n;
	    }
	    i++;
	}

    }

    return NULL;

}

/* rename a node and recursively reindex if necessary */
BsNode* bsRenameNode(BsDict* dict, BsNode* node, const char* newname) {

    if(node != NULL && node->parent != NULL && newname != NULL) {

	/* no renaming of array members */
	if(node->parent->type == BS_NODE_ARRAY) {
	    return NULL;
	}

	/* so we don't strlen twice... */
	size_t sl = strlen(newname);

	/* name unchanged */
	if(!strncmp(newname, node->name, min(node->nameLen, sl))) {
	    return node;
	}

	/* generate new name */
	BsToken tok = { (char*)newname, sl, false };
	free(node->name);
	node->name = getTokenData(&tok);
	node->nameLen = sl;

	uint32_t newhash = BS_MIX_HASH(xxHash32(node->name, node->nameLen), node->parent->hash, node->nameLen);

	/* no need to rehash in the rare case that hash did not change */
	if(newhash != node->hash) {
	    bsNodeWalk(dict, node, NULL, NULL, bsRehashCallback);
	}

	return node;

    }

    return NULL;

}

/*
 * dictionary / node duplication callback. the feedback is a pointer to the new node
 * that was created before we started iterating over its children - thanks to
 * the feedback mechanism we always add to the correct node.
 */
static void *bsDupCallback(BsDict *dict, BsNode *node, void* user, void* feedback, bool* stop) {

    BsDict *dest = user;
    BsNode* target = feedback;

    /* bsCreate is called as opposed to _bsCreate, which takes care of duplicating the name */
    BsNode* newnode = bsCreateNode(dest, target, node->type, node->name, node->value);

    if(newnode != NULL) {
	newnode->flags = node->flags;
    }

    return newnode;

}

/* copy node to new parent, under (optionally) new name */
BsNode* bsCopyNode(BsDict* dict, BsNode* node, BsNode* newparent, const char* newname) {

    /* ! this whole thing is a total hack and will need to be resolved if this is ever made thread-safe ! */

    /* temporarily set source node's name to new name */
    char* oldname = node->name;

    if(newparent == NULL) {
	return NULL;
    }

    node->name = (newname != NULL && strncmp(newname, node->name, min(node->nameLen, strlen(newname)))) ?
		    (char*)newname : oldname;

    /* callback takes care of the deep copy */
    bsNodeWalk(dict, node, dict, newparent, bsDupCallback);

    node->name = oldname;

    /* hack again - we have no way to grab the duplicate from the callback run, so we grab new parent's last child */
    return newparent->_lastChild;

}

/* move node to new parent, under (optionally) new name */
BsNode* bsMoveNode(BsDict* dict, BsNode* node, BsNode* newparent, const char* newname) {

    size_t sl = 0;

    if(newname != NULL) {
        sl = strlen(newname);
    }

    /* will not move root node and will not attach to NULL parent and will not set empty name */
    if(newparent == NULL || node->parent == NULL) {
	return NULL;
    }

    /* if parent is the same, this is a rename */
    if(node->parent == newparent) {

	/* only rename if new name differs */
	if(newname != NULL && strncmp(newname, node->name, min(node->nameLen, sl))) {
	    bsRenameNode(dict, node, newname);
	}

	return node;

    }

    /* shift about */
    LL_REMOVE_DYNAMIC(node->parent, node);
    if(node->parent->childCount > 0) {
	node->parent->childCount--;
    }
    LL_APPEND_DYNAMIC(newparent, node);
    node->parent = newparent;
    newparent->childCount++;

    /* change name if necessary */
    if(newname != NULL && strncmp(newname, node->name, min(node->nameLen, sl))) {
	BsToken tok = { (char*)newname, sl, false };
	free(node->name);
	node->name = getTokenData(&tok);
	node->nameLen = sl;
    }

    /* rehash */
    uint32_t newhash = BS_MIX_HASH(xxHash32(node->name, node->nameLen), node->parent->hash, node->nameLen);

    /* no need to rehash in the rare case that hash did not change */
    if(newhash != node->hash) {
	bsNodeWalk(dict, node, NULL, NULL, bsRehashCallback);
    }

    return node;

}

/* duplicate a dictionary, give new name to resulting dictionary */
BsDict* bsDuplicate(BsDict *source, const char* newname, const uint32_t newflags) {

    BsDict* dest = bsCreate(newname, newflags);

    if(dest == NULL) {
	return NULL;
    }

    /* magic */
    bsNodeWalk(source, source->root, dest, dest->root, bsDupCallback);

    return dest;

}

/* test sink - return false to stop the test program after this */
bool bsTest(BsDict *dict) {

    return true;

}
