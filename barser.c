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


#include <stdlib.h>
#include <sys/types.h>
#include <stdbool.h>

#include <stdio.h>
#include <string.h>

#include "xalloc.h"
#include "rbt/st_inline.h"
#include "xxh.h"
#include "itoa.h"

#include "barser.h"
#include "barser_index.h"
#include "barser_defaults.h"

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
		state.tokenCount = 0;

/* shorthand to get token data and quoted check */
#define td(n) getTokenData(&state.tokenCache[n])
#define ts(n) state.tokenCache[n].data
#define tq(n) state.tokenCache[n].quoted
#define tl(n) state.tokenCache[n].len

/* get the existing child of node 'parent' named as token #n in cache */
#define gch(parent, n) getNodeChild(dict, parent, state.tokenCache[n].data, state.tokenCache[n].len)

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
 */
static int
_bsDumpNode(FILE* fl, BsNode *node, int level)
{
    int ret = 0;
    bool noIndentArray = true;

    /* allocate enough indentation space for current level + 1 */
    int maxwidth = (level + 1) * BS_INDENT_WIDTH;
    char indent[ maxwidth + 1];
    BsNode *n = NULL;
    bool inArray = (node->parent != NULL && node->parent->type == BS_NODE_ARRAY);
    bool inCollection = (node->parent != NULL && node->parent->type == BS_NODE_COLLECTION);
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
    if(node->type != BS_NODE_COLLECTION) {
	ret = fprintf(fl, "%s", inArray && noIndentArray && !hadBranchSibling ? " " : indent);
	if(ret < 0) {
	    return -1;
	}
	if(node->parent != NULL) {

	    if(!inArray) {
		if(inCollection) {
		    ret = bsDumpQuoted(fl, node->parent->name, node->parent->flags & BS_QUOTED_NAME);
		    if(ret < 0) {
			return -1;
		    }
		    ret = fprintf(fl, " ");
		    if(ret < 0) {
			return -1;
		    }

		}
		ret = bsDumpQuoted(fl, node->name, node->flags & BS_QUOTED_NAME);
		if(ret < 0) {
		    return -1;
		}
		if(inCollection) {
		    if(node->childCount == 1) {
			BsNode *tmp = (BsNode*)node->_firstChild;
			if(tmp != NULL && tmp->type == BS_NODE_LEAF) {
			    ret = fprintf(fl, " ");
			    if(ret < 0) {
				return -1;
			    }
			    ret = bsDumpQuoted(fl, tmp->name, tmp->flags & BS_QUOTED_NAME);
			    if(ret < 0) {
				return -1;
			    }
			    if(tmp->value != NULL) {
				ret = fprintf(fl, " ");
				if(ret < 0) {
				    return -1;
				}
				ret = bsDumpQuoted(fl, tmp->value, tmp->flags & BS_QUOTED_VALUE);
				if(ret < 0) {
				    return -1;
				}

			    }
			    ret = fprintf(fl, "%c\n", BS_ENDVAL_CHAR);
			    if(ret < 0) {
				return -1;
			    }
			    return 0;
			}

		    }
		}
	    }
	}

    }

    if(node->childCount == 0) {
	if(node->type != BS_NODE_ROOT) {

	    if(node->value && strlen(node->value)) {

		if(!inArray) {
		    ret = fprintf(fl, " ");
		    if(ret < 0) {
			return -1;
		    }
		}

		bsDumpQuoted(fl, node->value, node->flags & BS_QUOTED_VALUE);
		if(ret < 0) {
		    return -1;
		}

		if(!inArray) {
		    ret = fprintf(fl, "%c", BS_ENDVAL_CHAR);
		    if(ret < 0) {
			return -1;
		    }
		}

		if(!inArray) {
		    ret = fprintf(fl, "\n");
		    if(ret < 0) {
			return -1;
		    }
		}
	    } else {
		if(!inArray) {
		    ret = fprintf(fl, "%c", BS_ENDVAL_CHAR);
		    if(ret < 0) {
			return -1;
		    }
		}
		if(!isArray || !noIndentArray) {
		    ret = fprintf(fl, "\n");
		    if(ret < 0) {
			return -1;
		    }
		}

	    }
	
	}

    } else {

	if(node->type != BS_NODE_ROOT) {
	    if(node->type != BS_NODE_COLLECTION) {
		ret = fprintf(fl,  "%s%c", strlen(node->name) ? " " : "",
		    isArray ? BS_STARTARRAY_CHAR : BS_STARTBLOCK_CHAR);
		if(ret < 0) {
		    return -1;
		}
		if(!isArray || !noIndentArray) {
		    ret = fprintf(fl, "\n");
		    if(ret < 0) {
			return -1;
		    }
		}
	    }
	}

	/* increase indent in case if we want to print something here later */
	memset(indent + level * BS_INDENT_WIDTH, BS_INDENT_CHAR, BS_INDENT_WIDTH);
	LL_FOREACH_DYNAMIC(node, n) {

	    if(node->type == BS_NODE_COLLECTION) {
		ret = _bsDumpNode(fl, n, level);
		if(ret < 0) {
		    return -1;
		}
	    } else {
		if(n->parent != NULL && n->parent->parent != NULL && n->parent->parent->type == BS_NODE_COLLECTION) {
//		    fprintf(fl, "//boom\n");
		}
		ret = _bsDumpNode(fl, n, level + (node->parent != NULL));
		if(ret < 0) {
		    return -1;
		}
	    }
	}
	/* decrease indent again */
	memset(indent + (level) * BS_INDENT_WIDTH, '\0', BS_INDENT_WIDTH);
	if(node->type != BS_NODE_ROOT) {
	    if(node->type != BS_NODE_COLLECTION) {
		ret = fprintf(fl, "%s", isArray && noIndentArray ? " " : indent);
		if(ret < 0) {
			return -1;
		}
		if(isArray) {
		    ret = fprintf(fl, "%c", BS_ENDARRAY_CHAR);
		    if(ret < 0) {
			return -1;
		    }
		    if(!inArray) {
			ret = fprintf(fl, "%c", BS_ENDVAL_CHAR);
			if(ret < 0) {
			    return -1;
			}
		    }
		} else {
		    ret = fprintf(fl, "%c", BS_ENDBLOCK_CHAR);
		    if(ret < 0) {
			return -1;
		    }

		}
	    }
	}
	if(node->type != BS_NODE_COLLECTION) {
	    ret = fprintf(fl, "\n");
	    if(ret < 0) {
		return -1;
	    }
	}

    }

    return 0;

}

/* dump a single node recursively to file, return number of bytes written */
int bsDumpNode(FILE* fl, BsNode *node) {
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
 * Create a node in dict with parent parent of type type using name 'name' of length 'namelen'.
 * This is the internal version of this function (underscore), which only attaches the name
 * to the node. If called directly, the name should have been passed throuh getTokenData() first,
 * so that the name is guaranteed not to come from a buffer that will later be destroyed.
 *
 */
static inline BsNode* _bsCreateNode(BsDict *dict, BsNode *parent, const unsigned int type, char* name, const size_t namelen)
{

    BsNode *ret;
    size_t slen = 0;

    if(dict == NULL) {
	return NULL;
    }

    xmalloc(ret, sizeof(BsNode));

    if(ret == NULL) {
	return NULL;
    }

    ret->parent = parent;
    ret->type = type;

    /* could have calloc'd, but... was d */
    ret->flags = 0;
    ret->value = NULL;
    ret->childCount = 0;
    ret->nameLen = 0;
    ret->_firstChild = ret->_lastChild = ret->_next = ret->_prev = NULL;
    ret->_first = NULL;
#ifdef COLL_DEBUG
    ret->collcount = 0;
#endif /* COLL_DEBUG */
    if(parent != NULL) {

	/* if we are adding an array member, call it by number, ignoring the name */
	if(parent->type == BS_NODE_ARRAY) {
	    char numname[INT_STRSIZE + 1];
	    /* major win over snprintf, 30% total performance difference for citylots.json */
	    char* endname = u32toa(numname, parent->childCount);
	    slen = endname - numname;
	    xmalloc(ret->name, slen + 1);
	    memcpy(ret->name, numname, slen);
	} else {
	    if(name == NULL) {
		goto onerror;
	    }
	    ret->name = name;
	    /* the extra byte is for a trailing '/', not for NUL */
	    slen = namelen;
	}

	/* mix this node's name's hash with parent's hash */
	ret->hash = BS_MIX_HASH(xxHash32(ret->name, slen), parent->hash, slen);

	ret->nameLen = slen;

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
BsNode* bsCreateNode(BsDict *dict, BsNode *parent, const unsigned int type, const char* name) {

    char* out = NULL;
    size_t slen = 0;
    if(parent != NULL && parent->type == BS_NODE_ARRAY) {

	return _bsCreateNode(dict, parent, type, NULL, 0);

    }

    if(name == NULL || ((slen = strlen(name)) == 0)) {
	xmalloc(out, 1);
    } else {
        slen = strlen(name);
        xmalloc(out, slen + 1);
        memcpy(out, name, slen);
    }

    *(out + slen) = '\0';

    return _bsCreateNode(dict, parent, type, out, slen);

}

/* [get|check if] parent node has a child with specified name */
static inline BsNode* getNodeChild(BsDict* dict, BsNode *parent, const char* name, const size_t namelen) {

    uint32_t hash;
    BsNode* n;
    LList *l;
    LListMember *m;

    if(name != NULL && namelen > 0) {

	hash = BS_MIX_HASH(xxHash32(name, namelen), parent->hash, namelen);

	/* grab node from index if we can */
	if(!(dict->flags & BS_NOINDEX)) {

	    l = bsIndexGet(dict->index, hash);

	    if(l != NULL) {

		LL_FOREACH_DYNAMIC(l, m) {

		    n = m->value;
		    if(n != NULL && n->parent == parent) {
			return n;
		    }

		}

	    }
	/* otherwise do a naive search */
	} else {

	    LL_FOREACH_DYNAMIC(parent, n) {
		if(n->hash == hash && n->nameLen == namelen && !strncmp(name, n->name, namelen)) {
		    return n;
		}
	    }
	
	}

    }

    return NULL;

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
    _bsCreateNode(ret, NULL, BS_NODE_ROOT,NULL,0);

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

/* free a dictionary */
void bsFree(BsDict *dict) {

    if(dict == NULL) {
	return;
    }

    if(dict->root != NULL) {
	bsDeleteNode(dict, dict->root);
	bsFreeNode(dict->root);
    }

    if(dict->name != NULL) {
	free(dict->name);
    }

    if(!(dict->flags & BS_NOINDEX) && dict->index != NULL) {
	bsIndexFree(dict->index);
    }

    free(dict);

}

static inline char* unescapeString(char *str) {

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

    return str;

}

/* walk through string @in, and write to + return next token between the 'sep' character */
static inline BsToken* unescapeToken(BsToken* out, char** in, const char sep) {

    size_t ssize = BS_QUOTED_STARTSIZE;
    bool captured = false;
    int c;

    /* skip past the separator and anything random */
    while( (((c = **in) == sep) || !cclass(BF_TOK|BF_EXT)) && c != '\0') {
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

/* node rehash callback */
static void* bsRehashCallback(BsDict *dict, BsNode *node, void* user, void* feedback, bool* cont) {

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

    bool cont = true;

    BsNode *n, *o;

    void* feedback1 = callback(dict, node, user, feedback, &cont);

    if(!cont) {
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

/* print error hint */
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

	fprintf(stderr, "Parser error: ");
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

	    default:
		fprintf(stderr, "Unexpected parser error 0x%x\n", state->parseError);
	}

	fprintf(stderr," at line %zd position %zd:\n\n", state->lineno, state->linepos);
	bsErrorHint(state);

    }

}

/* string scanner state machine */
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
		while(c != qchar) {

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
 * version for JSON which has none of that.
 */
BsState bsParse(BsDict *dict, char *buf, const size_t len) {

    /* node stack, so we can return n levels up if we created multiple in one go */
    PST_DECL(nodestack, BsNode*, 16);

    BsNode *head;
    BsNode *newnode, *newnode2;
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

		if(++state.tokenCount == BS_MAX_TOKENS) {
		    /* we can have as many tokens as we want when in an array, add them in batches */
		    if(head->type == BS_NODE_ARRAY) {
			for(int i = 0; i < state.tokenCount; i++) {
			    newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, NULL, 0);
			    newnode->value = td(i);
			    newnode->flags = BS_QUOTED_VALUE & tq(i);
			}
			state.tokenCount = 0;
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
		    for(int i = 0; i < state.tokenCount; i++) {
			newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, NULL, 0);
			newnode->value = td(i);
			newnode->flags = BS_QUOTED_VALUE & tq(i);
		    }

		    /* now enter into an unnamed branch which is a new member of the array */
		    PST_PUSH_GROW(nodestack, head); /* save current position */
		    newnode = _bsCreateNode(dict, head, BS_NODE_BRANCH, NULL, 0);
		    head = newnode;

		} else {
		    switch(state.tokenCount) {
			case 1:
			    PST_PUSH_GROW(nodestack, head);
			    /*
			     * the macros td, tq and tl are defined at the top of this file. They simply
			     * grab the data, quoted field and len field from the given item in token cache.
			     */
			    newnode = _bsCreateNode(dict, head, BS_NODE_BRANCH, td(0), tl(0));
			    newnode->flags = BS_QUOTED_NAME & tq(0);
			    head = newnode;
			    break;
			case 2:
			    PST_PUSH_GROW(nodestack, head);
			    if((newnode = gch(head, 0)) == NULL) {
				newnode = _bsCreateNode(dict, head, BS_NODE_COLLECTION, td(0), tl(0));
			    }
			    newnode->flags = BS_QUOTED_NAME & tq(0);
			    newnode = _bsCreateNode(dict, newnode, BS_NODE_BRANCH, td(1), tl(1));
			    newnode->flags = BS_QUOTED_NAME & tq(1);
			    head = newnode;
			    break;
			case 3:
			    PST_PUSH_GROW(nodestack, head);
			    if((newnode = gch(head, 0)) == NULL) {
				newnode = _bsCreateNode(dict, head, BS_NODE_COLLECTION, td(0), tl(0));
			    }
			    newnode->flags = BS_QUOTED_NAME & tq(0);
			    if((newnode2 = gch(newnode, 1)) == NULL) {
				newnode2 = _bsCreateNode(dict, newnode, BS_NODE_COLLECTION, td(1), tl(1));
			    }
			    newnode2->flags = BS_QUOTED_NAME & tq(1);
			    newnode = _bsCreateNode(dict, newnode2, BS_NODE_BRANCH, td(2), tl(2));
			    newnode->flags = BS_QUOTED_NAME & tq(2);
			    head = newnode;
			    break;
			/* unnamed branch? only at root level and only once */
			case 0:
			    if(head == dict->root && nodestack_sh == 0) {
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
		state.tokenCount = 0;
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

		    switch(state.tokenCount) {

			case 1:
			    newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, NULL, 0);
			    newnode->value = td(0);
			    newnode->flags = BS_QUOTED_VALUE & tq(0);
			    break;
			/* this is only a courtesy thing. array members are always unnamed - we only take the value */
			case 2:
			    newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, NULL, 0);
			    newnode->value = td(1);
			    newnode->flags = BS_QUOTED_VALUE & tq(1);
			    break;
			case 0:
			    break;
			default:
			    state.parseEvent = BS_ERROR;
			    state.parseError = BS_PERROR_TOKENS;
			break;
		    }

		} else {

		    switch(state.tokenCount) {

			case 1:
			    newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, td(0), tl(0));
			    newnode->flags = BS_QUOTED_NAME & tq(0);
			    break;
			case 2:
			    /* two tokens, node does not exist at current parent = leaf with value */
			    if((newnode = gch(head, 0)) == NULL) {
				newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, td(0), tl(0));
				newnode->flags = BS_QUOTED_NAME & tq(0);
				newnode->value = td(1);
				newnode->flags |= BS_QUOTED_VALUE & tq(1);
			    /* two tokens, node does exist at parent */
			    } else {
				/* convert existing node to collection */
				newnode->type = BS_NODE_COLLECTION;
				newnode->flags = BS_QUOTED_NAME & tq(0);
				/* convert existing value to new leaf */
				if(newnode->value != NULL) {
				    newnode2 = _bsCreateNode(dict, newnode, BS_NODE_LEAF, newnode->value, 0);
				    newnode2->flags = newnode->flags;
				    /* remove value from existing node */
				    newnode->value = NULL;
				}
				/* create a new leaf with no value */
				newnode2 = _bsCreateNode(dict, newnode, BS_NODE_LEAF, td(1), tl(1));
				newnode2->flags = BS_QUOTED_NAME & tq(1);
			    }
			    break;
			case 3:
			    if((newnode = gch(head, 0)) == NULL) {
				newnode = _bsCreateNode(dict, head, BS_NODE_COLLECTION, td(0), tl(0));
			    }
			    newnode->flags = BS_QUOTED_NAME & tq(0);
			    if((newnode2 = gch(newnode, 1)) == NULL) {
				newnode2 = _bsCreateNode(dict, newnode, BS_NODE_BRANCH, td(1), tl(1));
			    }
			    newnode2->flags = BS_QUOTED_NAME & tq(1);
			    newnode = _bsCreateNode(dict, newnode2, BS_NODE_LEAF, td(2), tl(2));
			    newnode->flags |= BS_QUOTED_NAME & tq(2);
			    break;
			case 4:
			    if((newnode = gch(head, 0)) == NULL) {
				newnode = _bsCreateNode(dict, head, BS_NODE_COLLECTION, td(0), tl(0));
			    }
			    newnode->flags = BS_QUOTED_NAME & tq(0);
			    if((newnode2 = gch(newnode, 1)) == NULL) {
				newnode2 = _bsCreateNode(dict, newnode, BS_NODE_BRANCH, td(1), tl(1));
			    }
			    newnode2->flags = BS_QUOTED_NAME & tq(1);
			    newnode = _bsCreateNode(dict, newnode2, BS_NODE_LEAF, td(2), tl(2));
			    newnode->flags = BS_QUOTED_NAME & tq(2);
			    newnode->value = td(3);
			    newnode->flags |= BS_QUOTED_VALUE & tq(3);
			    break;
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
				newnode = _bsCreateNode(dict, head, BS_NODE_BRANCH, td(0), tl(0));
				newnode->flags |= BS_QUOTED_NAME & tq(0);

				BsNode *tmphead = newnode;

				for(int i = 1; i < state.tokenCount; i++) {

				    newnode = _bsCreateNode(dict, tmphead, BS_NODE_LEAF, td(i), tl(i));
				    newnode->flags = BS_QUOTED_NAME & tq(i);

				    if(++i < state.tokenCount) {
					newnode->value = td(i);
					newnode->flags |= BS_QUOTED_VALUE & tq(i);
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

		state.tokenCount = 0;
		break;

	    /* array start block i.e. '[' */
	    case BS_GOT_ARRAY:

		/* handle nested arrays - same case as GOT_BLOCK in an array */
		if(head->type == BS_NODE_ARRAY) {

		    /* first insert any existing tokens as array leaves */
		    for(int i = 0; i < state.tokenCount; i++) {
			newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, NULL, 0);
			newnode->value = td(i);
			newnode->flags = BS_QUOTED_VALUE & tq(i);
		    }

		    /* now enter into an unnamed array which is a new member of the upper array */
		    PST_PUSH_GROW(nodestack, head); /* save current position */
		    newnode = _bsCreateNode(dict, head, BS_NODE_ARRAY, NULL,0);
		    head = newnode;

		} else {

		    switch(state.tokenCount) {
			case 1:
			    PST_PUSH_GROW(nodestack, head);
			    newnode = _bsCreateNode(dict, head, BS_NODE_ARRAY, td(0), tl(0));
			    newnode->flags = BS_QUOTED_NAME & tq(0);
			    head = newnode;
			    break;
			case 2:
			    PST_PUSH_GROW(nodestack, head);
			    newnode = _bsCreateNode(dict, head, BS_NODE_BRANCH, td(0), tl(0));
			    newnode->flags = BS_QUOTED_NAME & tq(0);
			    newnode = _bsCreateNode(dict, newnode, BS_NODE_ARRAY, td(1), tl(1));
			    newnode->flags = BS_QUOTED_NAME & tq(1);
			    head = newnode;
			    break;
			case 3:
			    PST_PUSH_GROW(nodestack, head);
			    if((newnode = gch(head, 0)) == NULL) {
				newnode = _bsCreateNode(dict, head, BS_NODE_COLLECTION, td(0), tl(0));
			    }
			    newnode->flags = BS_QUOTED_NAME & tq(0);
			    newnode = _bsCreateNode(dict, newnode, BS_NODE_BRANCH, td(1), tl(1));
			    newnode->flags = BS_QUOTED_NAME & tq(1);
			    newnode = _bsCreateNode(dict, newnode, BS_NODE_ARRAY, td(2), tl(2));
			    newnode->flags = BS_QUOTED_NAME & tq(2);
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

		state.tokenCount = 0;

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
		for(int i = 0; i < state.tokenCount; i++) {
		    newnode = _bsCreateNode(dict, head, BS_NODE_LEAF, NULL, 0);
		    newnode->value = td(i);
		    newnode->flags = BS_QUOTED_VALUE & tq(i);
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

		state.tokenCount = 0;

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

	if(out != NULL) {
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

/* find a descendant of node based on path, and verify that path matches */
BsNode* bsNodeGet(BsDict* dict, BsNode *node, const char* qry) {

    BsToken tok;
    uint32_t hash;
    BsNode *current = node;
    char* cqry;
    char *marker;

    if(qry != NULL) {

	hash = bsGetPathHash(node, qry);
        cqry = getCleanQuery(qry);
	if(cqry != NULL) {

	    /* if the dictionary is indexed, search in index */
	    if(!(dict->flags & BS_NOINDEX)) {

		LList* l =  bsIndexGet(dict->index, hash);
		if(l != NULL) {
		    LListMember *m;
		    LL_FOREACH_DYNAMIC(l, m) {

			current = m->value;

			BS_GETNP(current, path);
			if(!strcmp(cqry, path)) {
			    free(cqry);
			    return current;
			}

		    }
		}
	    /* otherwise do a naive search */
	    } else {

		marker = cqry;
		/* iterate over tokens, moving down the tree as we find children token by token */
		while((current != NULL) && unescapeToken(&tok, &marker, BS_PATH_SEP)) {

		    current = getNodeChild(dict, current, tok.data, tok.len);
		    free(tok.data);

		}

		free(cqry);
		return current;

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


/* rename a node and recursively reindex if necessary */
void bsRenameNode(BsDict* dict, BsNode* node, const char* newname) {

    if(node != NULL && node->parent != NULL && newname != NULL) {

	/* no renaming of array members */
	if(node->parent->type == BS_NODE_ARRAY) {
	    return;
	}

	uint32_t newhash = BS_MIX_HASH(xxHash32(node->name, node->nameLen), node->parent->hash, node->nameLen);
	BsToken tok = { (char*)newname, strlen(newname), false };
	free(node->name);
	node->name = getTokenData(&tok);
	node->nameLen = tok.len;

	/* no need to rehash in the rare case that hash did not change */
	if(newhash != node->hash) {
	    bsNodeWalk(dict, node, NULL, NULL, bsRehashCallback);
	}

    }

}

/*
 * dictionary duplication callback. the feedback is a pointer to the new node
 * that was created before we started iterating over its children - thanks to
 * the feedback mechanism we always add to the correct node.
 */
static void *bsDupCallback(BsDict *dict, BsNode *node, void* user, void* feedback, bool* cont) {

    BsDict *dest = user;
    BsNode* target = feedback;

    BsNode* newnode = bsCreateNode(dest, target, node->type, node->name);

    if(newnode != NULL) {
	newnode->flags = node->flags;
	/* duplicate value */
	if(node->type == BS_NODE_LEAF && node->value != NULL) {
	    size_t vlen = strlen(node->value);
	    xmalloc(newnode->value, vlen + 1);
	    memcpy(newnode->value, node->value, vlen);
	    newnode->value[vlen] = '\0';
	}
    }

    return newnode;

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
