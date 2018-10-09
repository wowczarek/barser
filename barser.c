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

#define _POSIX_SOURCE /* because fileno etc. */
#define _XOPEN_SOURCE 500 /* because strdup */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

//#include <unistd.h>
#include <string.h>

#include "rbt/st_inline.h"
#include "barser.h"
#include "barser_defaults.h"

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

/* min, max, everybody needs min/max */
#ifndef min
#define min(a,b) ((a < b) ? (a) : (b))
#endif

#ifndef max
#define max(a,b) ((a > b) ? (a) : (b))
#endif

/* shorthand to dump the token stack contents */
#define st_dumpstrings(name)\
		fprintf(stderr, "String stack with %zu items: ", name##_sh);\
		for(int i = 0; i < name##_sh; i++) {\
		    fprintf(stderr, "'%s' ", name[i]);\
		}\
		fprintf(stderr, "\n");

/* shorthand to clean up the token stack */
#define tokencleanup()\
		DST_FLUSH(quotestack);\
		PST_FREEDATA(tokenstack);\
		state.tokenCount = 0;

/* string scan state machine */
enum {
    BP_NOOP = 0,		/* do nothing (?) */
    BP_SKIP_WHITESPACE,		/* skipping whitespace (newlines are part of this) */
    BP_SKIP_NEWLINE,		/* skipping newlines when explicitly required */
    BP_GET_TOKEN,		/* acquiring a token */
    BP_GET_QUOTED,		/* acquiring a quoted / escaped string */
    BP_SKIP_COMMENT,		/* skipping a comment until after next newline */
    BP_SKIP_MLCOMMENT		/* skipping a multiline comment until end of comment */
};

/* parser events */
enum {
    BP_NONE = 0,		/* nothing happened, keep scanning */
    BP_GOT_TOKEN,	/* received a string */
    BP_GOT_QUOTED,	/* received a quoted string */
    BP_GOT_ENDVAL,	/* received "end of value", such as ';' */
    BP_GOT_BLOCK,	/* received beginning of block, such as '{' */
    BP_END_BLOCK,	/* received end of block, such as '}' */
    BP_GOT_ARRAY,	/* received opening of an array, such as '[' */
    BP_END_ARRAY,	/* received end of array, such as ']' */
    BP_GOT_EOF,		/* received EOF */
    BP_ERROR		/* parse error */
};

/* 'c' class check shorthand, assumes the presence of 'c' int variable */
#define cclass(cl) (chflags[c] & (cl))

/* save state when we encounter a section that can have unmatched or unterminated bounds */
#define savestate(st) 		st->slinestart = st->linestart;\
				st->slineno = st->lineno;\
				st->slinepos = st->linepos;
/* restore state, say when printing an error */
#define restorestate(st) 	st->linestart = st->slinestart;\
				st->lineno = st->slineno;\
				st->linepos = st->slinepos;

/* initialise parser state */
static void initBarserState(BarserState *state, char* buf, const size_t bufsize) {

    state->current = buf;
    state->prev = '\0';
    state->c = buf[0];
    state->end = buf + bufsize;

    state->str = NULL;
    state->strstart = NULL;
    state->linestart = buf;

    state->slinestart = buf;

    state->linepos = 0;
    state->lineno = 1;

    state->slinepos = 0;
    state->slineno = 1;

    state->scanState = BP_SKIP_WHITESPACE;

    state->parseEvent = BP_NONE;
    state->parseError = BP_PERROR_NONE;

    state->tokenCount = 0;

}

/* fetch next character from buffer and advance, return it as int or EOF if end reached */
static inline int barserForward(BarserState *state) {

    state->prev = *(state->current++);
    if(state->current == state->end) {
	return EOF;
    }

    int c = *state->current;
    state->c = c;

    if(c == '\0') {
	return EOF;
    }

    /* we have a newline */
    if(cclass(BF_NLN)) {
	/* if we came across two different newline characters, advance line number only once */
	if(!(chflags[state->prev] & BF_NLN) || c == state->prev) {
	    state->linestart = state->current;
	    state->lineno++;
	    state->linepos = 0;
	}

    } else {
	state->linepos++;
    }

    return c;

}

/* peek at the next character without moving forward */
static inline int barserPeek(BarserState *state) {

    if(state->current == state->end) {
	return EOF;
    }

    return (int)*(state->current + 1);

}

/* allocate buffer. put file in buffer. return size. */
size_t getFileBuf(char **out, const char *fileName) {

    char *buf = NULL;
    size_t size;
    FILE *fl = fopen(fileName, "r");

    if(fl == NULL || fseek(fl, 0, SEEK_END) < 0 || (size = ftell(fl)) < 0) {
	*out = 0;
	return 0;
    }

    rewind(fl);

    buf = malloc(size + 1);

    if(buf == NULL) {
	*out = NULL;
	return 0;
    }

    size_t ret = fread(buf, 1, size, fl);
    if(ret < size) {

	if(feof(fl)) {
	    fprintf(stderr, "Error: Truncated data while reading '%s': file size %zu bytes, read %zu bytes\n", fileName, size, ret);
	}

	if(ferror(fl)) {
	    fprintf(stderr, "Error while reading '%s': file size %zu bytes, read %zu bytes\n", fileName, size, ret);
	}

	free(buf);
	return 0;

    }
    buf[size] = EOF;

    fclose(fl);

    *out = buf;

    return(size + 1);
}

/* get the full path of node */
#if 0
size_t getCckConfigNodePath(CckConfigNode* node, char * path) {

    CckConfigNode *tmpNode;
    size_t len = 0;
    char *marker = path;

    if(node == NULL || path == NULL) {
	return 0;
    }

    marker = path + node->_pathLen;;

    for(len = 0, tmpNode = node; tmpNode->parent != NULL; tmpNode = tmpNode->parent) {
	size_t sl = strlen(tmpNode->name);
	marker -= sl;
	len += sl + 1;
	strncpy(marker, tmpNode->name, sl);
	if(marker > path) {
	    marker--;
	}
	    *(marker) = '/';
    }

    path[len] = '\0';
    return len + 1;
}

static inline bool cckConfigNodePathMatch(CckConfigNode *node, const char *path) {

    /* with a stale index, we may be asked to compare a node that has been removed */
    if(node == NULL) {
	return false;
    }

    /* if path length does not match, it cannot be a match */
    if(strlen(path) != (node->_pathLen)) {
	return false;
    } else {
//	BP_GETNP(node, pth);
	return !strncmp(pth, path, node->_pathLen);
    }
}

#endif

static inline int printQuoted(FILE* fl, char *src, bool quoted) {

    int ret;
    int c;

    if(quoted) {

	ret = fprintf(fl, "%c", BP_DBLQUOTE_CHAR);
	if(ret < 0) {
	    return -1;
	    }

	for(char *marker = src; c = *marker, c != '\0'; marker++) {
	    /* since we we only print with double quotes, do not escape single quotes */
	    if(cclass(BF_ESC) && c != BP_SGLQUOTE_CHAR) {
		ret = fprintf(fl, "%c%c", BP_ESCAPE_CHAR, esccodes[c]);
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

	ret = fprintf(fl, "%c", BP_DBLQUOTE_CHAR);

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
 * and stab myself in the neck repeatedly with it. But it will have to do for now.
 */
static int
dumpBarserNode(FILE* fl, BarserNode *node, int level)
{
    int ret = 0;
    bool noIndentArray = true;

    /* allocate enough indentation space for current level + 1 */
    int maxwidth = (level + 1) * BP_INDENT_WIDTH;
    char indent[ maxwidth + 1];
    BarserNode *n = NULL;
    bool inArray = (node->parent != NULL && node->parent->type == BP_NODE_ARRAY);
    bool inCollection = (node->parent != NULL && node->parent->type == BP_NODE_COLLECTION);
    bool isArray = (node->type == BP_NODE_ARRAY);
    bool hadBranchSibling = inArray && node->_prev != NULL && node->_prev->type != BP_NODE_LEAF;

    /* fill up the buffer with indent char, but up to current level only */
    memset(indent, BP_INDENT_CHAR, maxwidth);
    memset(indent + level * BP_INDENT_WIDTH, '\0', BP_INDENT_WIDTH);
    /* yessir... */
    indent[maxwidth] = '\0';

    if(node->type != BP_NODE_COLLECTION) {
	ret = fprintf(fl, "%s", inArray && noIndentArray && !hadBranchSibling ? " " : indent);
	if(ret < 0) {
	    return -1;
	}
	if(node->parent != NULL) {

	    if(!inArray) {
		if(inCollection) {
		    ret = printQuoted(fl, node->parent->name, node->parent->flags & BP_QUOTED_NAME);
		    if(ret < 0) {
			return -1;
		    }
		    ret = fprintf(fl, " ");
		    if(ret < 0) {
			return -1;
		    }
		}

		ret = printQuoted(fl, node->name, node->flags & BP_QUOTED_NAME);
		if(ret < 0) {
		    return -1;
		}
	    }
	}

    }

    if(node->childCount == 0) {
	if(node->type != BP_NODE_ROOT) {

	    if(node->value && strlen(node->value)) {

		if(!inArray) {
		    ret = fprintf(fl, " ");
		    if(ret < 0) {
			return -1;
		    }
		}

		printQuoted(fl, node->value, node->flags & BP_QUOTED_VALUE);
		if(ret < 0) {
		    return -1;
		}

		if(!inArray) {
		    ret = fprintf(fl, "%c", BP_ENDVAL1_CHAR);
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
		    ret = fprintf(fl, "%c", BP_ENDVAL1_CHAR);
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

	if(node->type != BP_NODE_ROOT) {
	    if(node->type != BP_NODE_COLLECTION) {
		ret = fprintf(fl,  "%s%c", strlen(node->name) ? " " : "",
		    isArray ? BP_STARTARRAY_CHAR : BP_STARTBLOCK_CHAR);
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
	memset(indent + level * BP_INDENT_WIDTH, BP_INDENT_CHAR, BP_INDENT_WIDTH);
	LL_FOREACH_DYNAMIC(node, n) {

	    if(node->type == BP_NODE_COLLECTION) {
		ret = dumpBarserNode(fl, n, level);
		if(ret < 0) {
		    return -1;
		}
	    } else {
		ret = dumpBarserNode(fl, n, level + (node->parent != NULL));
		if(ret < 0) {
		    return -1;
		}
	    }
	}
	/* decrease indent again */
	memset(indent + (level) * BP_INDENT_WIDTH, '\0', BP_INDENT_WIDTH);
	if(node->type != BP_NODE_ROOT) {
	    if(node->type != BP_NODE_COLLECTION) {
		ret = fprintf(fl, "%s", isArray && noIndentArray ? " " : indent);
		if(ret < 0) {
			return -1;
		}
		if(isArray) {
		    ret = fprintf(fl, "%c", BP_ENDARRAY_CHAR);
		    if(ret < 0) {
			return -1;
		    }
		    if(!inArray) {
			ret = fprintf(fl, "%c", BP_ENDVAL1_CHAR);
			if(ret < 0) {
			    return -1;
			}
		    }
		} else {
		    ret = fprintf(fl, "%c", BP_ENDBLOCK_CHAR);
		    if(ret < 0) {
			return -1;
		    }

		}
	    }
	}
	if(node->type != BP_NODE_COLLECTION) {
	    ret = fprintf(fl, "\n");
	    if(ret < 0) {
		return -1;
	    }
	}

    }

    return 0;

}

/* dump the whole dictionary */
void dumpBarserDict(FILE* fl, BarserDict *dict) {

    dumpBarserNode(fl, dict->root, 0);

}

void
freeBarserNode(BarserNode *node)
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

/* create a new node in @dict, attached to @parent, of type @type with name @name */
BarserNode* createBarserNode(BarserDict *dict, BarserNode *parent, const unsigned int type, const char* name)
{
    tmpstr(myName, INT_STRSIZE);
    BarserNode *ret;

    if(dict == NULL) {
	return NULL;
    }

    ret = calloc(1, sizeof(BarserNode));

    if(ret == NULL) {
	return NULL;
    }

    ret->parent = parent;
    ret->type = type;

    if(parent != NULL) {
	/* if we are adding an array member, call it by number, ignoring the name */
	if(parent->type == BP_NODE_ARRAY) {
	    /* this cunt right here. 30% total performance difference for citylots.json */
	    snprintf(myName, INT_STRSIZE, "%d", parent->childCount);
	    ret->name = strdup(myName);
	} else {
	    if(name == NULL) {
		goto onerror;
	    }
	    ret->name = strdup(name);
	}

	ret->_pathLen = parent->_pathLen + strlen(name) + 1;

	LL_APPEND_DYNAMIC(parent, ret);
	parent->childCount++;

    } else {

	if(dict->root != NULL) {
	    fprintf(stderr, "Error: will not replace dictionary '%s' root with node '%s'\n", dict->name, name);
	    goto onerror;
	} else {
	    dict->root = ret;
	    ret->type = BP_NODE_ROOT;
	    ret->name = strdup("");
	}

    }

    ret->dict = dict;
    dict->nodecount++;
    return ret;

onerror:

    freeBarserNode(ret);
    return NULL;

}

unsigned int
deleteBarserNode(BarserDict *dict, BarserNode *node)
{

    if(dict == NULL || dict != node->dict) {
	return BP_NODE_WRONG_DICT;
    }

    if(node == NULL) {
	return BP_NODE_NOT_FOUND;
    }

//    BP_GETNP(node, path);

    /* TODO: remove from index */


    /* remove all children recursively first */
    for ( BarserNode *child = node->_firstChild; child != NULL; child = node->_firstChild) {
	deleteBarserNode(dict, child);
    }

    /* root node is persistent, otherwise remove node */
    if(node->parent != NULL) {
	LL_REMOVE_DYNAMIC(node->parent, node); /* remove self from parent's list */
	node->parent->childCount--;
	freeBarserNode(node);
    }

    dict->nodecount--;

    return BP_NODE_OK;
}


BarserDict*
createBarserDict(const char *name) {

    BarserDict *ret = calloc(1, sizeof(BarserDict));
    if(ret == NULL) {
	return NULL;
    }
    ret->name = strdup(name);
    /* create the root node */
    createBarserNode(ret, NULL, BP_NODE_ROOT,"");
    return ret;
}

void
freeBarserDict(BarserDict *dict) {

    if(dict == NULL) {
	return;
    }

    if(dict->root != NULL) {
	deleteBarserNode(dict, dict->root);
	freeBarserNode(dict->root);
    }

    if(dict->name != NULL) {
	free(dict->name);
    }
    free(dict);

}

/* print parser error */
void printBarserError(BarserState *state) {

    if(state->parseError == BP_PERROR_NONE) {

	fprintf(stderr, "No error: parsed successfully\n");
	return;

    } else {

	fprintf(stderr, "Parser error: ");
	switch (state->parseError) {
	    case BP_PERROR_EOF:

		switch(state->scanState) {
		    case BP_GET_QUOTED:
			fprintf(stderr, "Unterminated quoted string");
			restorestate(state);
			break;
		    case BP_SKIP_MLCOMMENT:
			fprintf(stderr, "Unterminated multiline comment");
			restorestate(state);
			break;
		    default:
			fprintf(stderr, "Unexpected EOF");
			break;
		}

		break;
	    case BP_PERROR_UNEXPECTED:
		fprintf(stderr, "Unexpected character: '%c' (0x%02x)", *state->current, *state->current);
		break;
	    case BP_PERROR_LEVEL:
		fprintf(stderr, "Unbalanced bracket (s) found");
		break;
	    case BP_PERROR_TOKENS:
		fprintf(stderr, "Too many consecutive identifiers");
		break;
	    case BP_PERROR_EXP_ID:
		fprintf(stderr, "Expected node name / identifier");
		break;
	    case BP_PERROR_UNEXP_ID:
		fprintf(stderr, "Unexpected node name / identifier");
		break;
	    case BP_PERROR_BLOCK:
		fprintf(stderr, "Unexpected block element");
		break;
	    case BP_PERROR_NULL:
		fprintf(stderr, "Dictionary object is NULL\n");
		return;

	    default:
		fprintf(stderr, "Unexpected parser error 0x%x\n", state->parseError);
	}

	fprintf(stderr," at line %zd position %zd:\n\n", state->lineno, state->linepos);

	size_t minwidth = min(state->linepos, BP_ERRORDUMP_LINEWIDTH / 2);
	char linebuf[BP_ERRORDUMP_LINEWIDTH + 1];
	char pointbuf[minwidth + 1];
	linebuf[minwidth] = '\0';
	pointbuf[minwidth] = '\0';
	memset(linebuf, 0, BP_ERRORDUMP_LINEWIDTH);
	memset(pointbuf, ' ', minwidth);
	pointbuf[minwidth - 1] = '^';
	memcpy(linebuf, state->linestart + (state->linepos - minwidth + 1), minwidth);

	for(size_t i = minwidth; (i < BP_ERRORDUMP_LINEWIDTH) && !chclass((int)state->linestart[i+1], BF_NLN); i++) {
	    linebuf[i] = state->linestart[i+1];
	}

	fprintf(stderr,"\t%s\n\t%s\n", linebuf, pointbuf);

    }

}

/* string scanner state machine */
static inline void barserScan(BarserState *state) {

    int qchar = BP_DBLQUOTE_CHAR;

    int c = *state->current;

    /* continue scanning until an event occurs */
    do {
	/* search for tokens or quoted strings, skip comments, etc. */
	switch (state->scanState) {

	    case BP_SKIP_WHITESPACE:
		while(cclass(BF_SPC | BF_NLN)) {
		    c = barserForward(state);
		}
		/* we have reached a multiline comment outer character... */
		if(c == BP_MLCOMMENT_OUT_CHAR) {
		    /* ...and the next character is the inner multiline comment character */
		    if(barserPeek(state) == BP_MLCOMMENT_IN_CHAR) {
			/* save state when entering a 'find closing character' type state */
			savestate(state);
			c = barserForward(state);
			state->scanState = BP_SKIP_MLCOMMENT;
			break;
		    }
		    /* ...and the next character is also a multiline comment outer character */
		    if(barserPeek(state) == BP_MLCOMMENT_OUT_CHAR) {
			c = barserForward(state);
			state->scanState = BP_SKIP_COMMENT;
			break;
		    }
		}
		state->scanState = BP_GET_TOKEN;
		break;

	    case BP_GET_TOKEN:
		state->strstart = state->current;
		int flags = (state->tokenCount == 0) ? BF_TOK : BF_TOK | BF_EXT;
		while(cclass(flags)) {
			c = barserForward(state);
		}
		if(state->current > state->strstart) {
		    state->str = calloc(state->current - state->strstart + 1, 1);
		    if(state->str == NULL) {
			goto failure;
		    }
		    memcpy(state->str, state->strstart, state->current - state->strstart);
		}
		/* raise a "got token" event */
		state->parseEvent = BP_GOT_TOKEN;
		state->scanState = BP_SKIP_WHITESPACE;
		break;

	    case BP_GET_QUOTED: {

		size_t ssize = BP_QUOTED_STARTSIZE;
		size_t slen = 0;
		state->str = malloc(ssize + 1);
		if(state->str == NULL) {
		    goto failure;
		}
		bool captured;
		while(c != qchar) {

		    if(c == BP_ESCAPE_CHAR) {
			c = barserForward(state);
			/* this is  an escape sequence */
			if(cclass(BF_ESS)) {
			    /* place the corresponding control char */
			    state->str[slen] = esccodes[c];
			    captured = true;
			}
		    }

		    if(captured) {
			captured = false;
		    } else {
			if(c == EOF) {
			    state->parseEvent = BP_ERROR;
			    state->parseError = BP_PERROR_EOF;
			    return;
			}
			state->str[slen] = c;
		    }
		    c = barserForward(state);
		    slen++;
		    if(slen == ssize) {

			ssize *= 2;

			state->str = realloc(state->str, ssize + 1);
			if(state->str == NULL) {
			    goto failure;
			}
		    }
		}
		c = barserForward(state);
		state->str[slen] = '\0';
		/* raise a "got quoted string" event */
		state->parseEvent = BP_GOT_QUOTED;
		state->scanState = BP_SKIP_WHITESPACE;

		} break;

	    case BP_SKIP_NEWLINE:
		while(cclass(BF_NLN)) {
		    c = barserForward(state);
		}
		state->scanState = BP_SKIP_WHITESPACE;
		break;

	    case BP_SKIP_COMMENT:
		while(!cclass(BF_NLN)) {
		    c = barserForward(state);
		}
		state->scanState = BP_SKIP_NEWLINE;
		break;

	    case BP_SKIP_MLCOMMENT:
		printf("skipping ml comment\n");
		while(c != BP_MLCOMMENT_OUT_CHAR && c != EOF) {
		    c = barserForward(state);
		}
		if(c == BP_MLCOMMENT_OUT_CHAR) {
		    if (state->prev == BP_MLCOMMENT_IN_CHAR) {
			/* end of comment */
			printf("end of ml comment\n");
			state->scanState = BP_SKIP_WHITESPACE;
		    }
		    /* keep moving on */
		    c = barserForward(state);
		} else if (c == EOF) {
		    state->parseEvent = BP_ERROR;
		    state->parseError = BP_PERROR_EOF;
		}
		break;

	    default:
		state->parseEvent = BP_ERROR;
		state->parseError = BP_PERROR;

	}

	/* if no event raised, check for control characters, raise parser events and move search state accordingly */
	if(state->parseEvent == BP_NONE) {
	    switch(c) {
		case BP_COMMENT_CHAR:
		    state->scanState = BP_SKIP_COMMENT;
		    c = barserForward(state);
		    break;
		case BP_SGLQUOTE_CHAR:
		case BP_DBLQUOTE_CHAR:
		    state->scanState = BP_GET_QUOTED;
		    /* save state when entering a 'find closing character' type state */
		    savestate(state);
		    /* because we can have two quotation mark types, we save the one we encountered */
		    qchar = c;
		    c = barserForward(state);
		    break;
		case BP_ENDVAL1_CHAR:
#ifdef BP_ENDVAL2_CHAR
		case BP_ENDVAL2_CHAR:
#endif
#ifdef BP_ENDVAL3_CHAR
		case BP_ENDVAL3_CHAR:
#endif
#ifdef BP_ENDVAL4_CHAR
		case BP_ENDVAL4_CHAR:
#endif
#ifdef BP_ENDVAL5_CHAR
		case BP_ENDVAL5_CHAR:
#endif
		    state->scanState = BP_SKIP_WHITESPACE;
		    state->parseEvent = BP_GOT_ENDVAL;
		    c = barserForward(state);
		    break;
		case BP_STARTBLOCK_CHAR:
		    savestate(state);
		    state->scanState = BP_SKIP_WHITESPACE;
		    state->parseEvent = BP_GOT_BLOCK;
		    c = barserForward(state);
		    break;
		case BP_ENDBLOCK_CHAR:
		    state->scanState = BP_SKIP_WHITESPACE;
		    state->parseEvent = BP_END_BLOCK;
		    c = barserForward(state);
		    break;
		case BP_STARTARRAY_CHAR:
		    savestate(state);
		    state->scanState = BP_SKIP_WHITESPACE;
		    state->parseEvent = BP_GOT_ARRAY;
		    c = barserForward(state);
		    break;
		case BP_ENDARRAY_CHAR:
		    state->scanState = BP_SKIP_WHITESPACE;
		    state->parseEvent = BP_END_ARRAY;
		    c = barserForward(state);
		    break;
		case '\0':
		case EOF:
		    /* this is a legal end of buffer, as opposed to unexpected EOF */
		    state->parseEvent = BP_GOT_EOF;
		    break;
		default:
		    if(cclass(BF_ILL)) {
			state->parseEvent = BP_ERROR;
			state->parseError = BP_PERROR_UNEXPECTED;
		    }
		break;
	    }
	}

    } while(state->parseEvent == BP_NONE);

    return;

failure:

    state->parseEvent = BP_ERROR;
    state->parseError = BP_PERROR;

    return;

}

/*
 * parse the contents of buf into dictionary dict, return last state.
 * a lot of this logic (different token number cases) is to allow consumption
 * of weirder formats like Juniper configuration.
 */
BarserState barseBuffer(BarserDict *dict, char *buf, const size_t len) {

    PST_DECL(tokenstack, char*, 16);
    PST_DECL(nodestack, BarserNode*, 16);
    DST_DECL(quotestack, unsigned int, 16);

    BarserNode *head;
    BarserNode *newnode;
    BarserState state;

    initBarserState(&state, buf, len);

    if(dict == NULL) {
	state.parseError = BP_PERROR_NULL;
	return state;
    }

    head = dict->root;
    PST_INIT(tokenstack);
    PST_INIT(nodestack);
    DST_INIT(quotestack);

    /* keep parsing until no more data or parser error encountered */
    while(state.parseEvent != BP_GOT_EOF && !state.parseError) {

	state.parseEvent = BP_NONE;
	state.parseError = BP_PERROR_NONE;

	/* scan state machine runs until it barfs an event */
	barserScan(&state);

	/* process parser event */
	switch(state.parseEvent) {

	    /* we got a token - push it onto the stack */
	    case BP_GOT_TOKEN:

		PST_PUSH_GROW(tokenstack, state.str);
		/* indicate that it's not quoted */
		DST_PUSH_GROW(quotestack, 0);
		state.tokenCount++;
		break;

	    /* we got a quoted string - push it onto the stack */
	    case BP_GOT_QUOTED:

		PST_PUSH_GROW(tokenstack, state.str);
		/* indicate that it's quoted */
		DST_PUSH_GROW(quotestack, ~0U);
		state.tokenCount++;
		break;

	    /* we got start of a block, i.e. '{' */
	    case BP_GOT_BLOCK:

		/*
		 * create different node arrangements based on token count,
		 * note that we push the current parent / head onto the stack,
		 * so that we can return to it when this block ends, rather than returning
		 * upwards to some nested node that is obviously not it.
		 */

		/* it's all different for arrays, because ' a b c { something }' means 4 nodes - 3 leaves and a branch member */
		if(head->type == BP_NODE_ARRAY) {

		    /* first insert any existing tokens as array leaves */
		    for(int i = 0; i < state.tokenCount; i++) {
			newnode = createBarserNode(dict, head, BP_NODE_LEAF, "");
			newnode->value = strdup(tokenstack[i]);
			newnode->flags |= BP_QUOTED_VALUE & quotestack[i];
		    }

		    /* now enter into an unnamed branch which is a new member of the array */
		    PST_PUSH_GROW(nodestack, head); /* save current position */
		    newnode = createBarserNode(dict, head, BP_NODE_BRANCH, "");
		    head = newnode;

		} else {
		    switch(state.tokenCount) {
			case 1:
			    PST_PUSH_GROW(nodestack, head);
			    newnode = createBarserNode(dict, head, BP_NODE_BRANCH, tokenstack[0]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[0];
			    head = newnode;
			    break;
			case 2:
			    PST_PUSH_GROW(nodestack, head);
			    newnode = createBarserNode(dict, head, BP_NODE_COLLECTION, tokenstack[0]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[0];
			    newnode = createBarserNode(dict, newnode, BP_NODE_BRANCH, tokenstack[1]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[1];
			    head = newnode;
			    break;
			case 3:
			    PST_PUSH_GROW(nodestack, head);
			    newnode = createBarserNode(dict, head, BP_NODE_COLLECTION, tokenstack[0]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[0];
			    newnode = createBarserNode(dict, newnode, BP_NODE_COLLECTION, tokenstack[1]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[1];
			    newnode = createBarserNode(dict, newnode, BP_NODE_BRANCH, tokenstack[2]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[2];
			    head = newnode;
			    break;
			/* unnamed branch? only at root level and only once */
			case 0:
			    if(head == dict->root && nodestack_sh == 0) {
				/* imaginary descent. This allows us to put empty {}s around the whole content */
				PST_PUSH_GROW(nodestack, head);
			    /* nope. */
			    } else {
				state.parseEvent = BP_ERROR;
				state.parseError = BP_PERROR_EXP_ID;
			    }
			    break;

			default:
			    break;
		    }
		}
		/* we don't need the tokens anymore - free the strings, reset the stacks and token count */
		tokencleanup();
		break;

	    /*
	     * end of block. remaining tokens do not need to be terminated,
	     * so if we have any, we fall through to endval case. If we have none,
	     * pop the last branching point off the stack and we're golden.
	     * without the fall through, this would have to be a duplicate of the endval case.
	     */
	    case BP_END_BLOCK:

		/* leftover tokens without end-value termination are not allowed */
		if(state.tokenCount == 0) {
		    /* WOOP WOOP, WIND SHEAR, BANK ANGLE, PULL UP, TOO LOW, TERRAIN, TERRAIN */
		    if(PST_EMPTY(nodestack)) {
			state.parseEvent = BP_ERROR;
			state.parseError = BP_PERROR_LEVEL;
			break;
		    }
		    head = PST_POP(nodestack);
		    break;
		}

		/* What?! nope. */
		if(head->type == BP_NODE_ARRAY) {
		    state.parseEvent = BP_ERROR;
		    state.parseError = BP_PERROR_BLOCK;
		    break;
		}

	    /* we encountered an end of value indication like ';' or ',' (JSON) */
	    case BP_GOT_ENDVAL:

		/* arrays are special that way, a single token is a leaf with a value */
		if(head->type == BP_NODE_ARRAY) {

		    switch(state.tokenCount) {

			case 1:
			    newnode = createBarserNode(dict, head, BP_NODE_LEAF, "");
			    newnode->value = strdup(tokenstack[0]);
			    newnode->flags |= BP_QUOTED_VALUE & quotestack[0];
			    break;
			case 2:
			    newnode = createBarserNode(dict, head, BP_NODE_LEAF, tokenstack[0]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[0];
			    newnode->value = strdup(tokenstack[1]);
			    newnode->flags |= BP_QUOTED_VALUE & quotestack[1];
			    break;
			case 0:
			    break;
			default:
			    state.parseEvent = BP_ERROR;
			    state.parseError = BP_PERROR_TOKENS;
			break;
		    }

		} else {

		    switch(state.tokenCount) {

			case 1:
			    newnode = createBarserNode(dict, head, BP_NODE_LEAF, tokenstack[0]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[0];
			    break;
			case 2:
			    newnode = createBarserNode(dict, head, BP_NODE_LEAF, tokenstack[0]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[0];
			    newnode->value = strdup(tokenstack[1]);
			    newnode->flags |= BP_QUOTED_VALUE & quotestack[1];
			    break;
			case 3:
			    newnode = createBarserNode(dict, head, BP_NODE_BRANCH, tokenstack[0]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[0];
			    newnode = createBarserNode(dict, newnode, BP_NODE_LEAF, tokenstack[1]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[1];
			    newnode->value = strdup(tokenstack[2]);
			    newnode->flags |= BP_QUOTED_VALUE & quotestack[2];
			    break;
			case 4:
			    newnode = createBarserNode(dict, head, BP_NODE_COLLECTION, tokenstack[0]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[0];
			    newnode = createBarserNode(dict, newnode, BP_NODE_BRANCH, tokenstack[1]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[1];
			    newnode = createBarserNode(dict, newnode, BP_NODE_LEAF, tokenstack[2]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[2];
			    newnode->value = strdup(tokenstack[3]);
			    newnode->flags |= BP_QUOTED_VALUE & quotestack[3];
			    break;
			case 0:
			    break;

			/* at least 5 tokens */
			default:

			    /* too many tokens */
			    if(state.tokenCount > BP_MAX_TOKENS) {

				state.parseEvent = BP_ERROR;
				state.parseError = BP_PERROR_TOKENS;

			    } else {

				/*
				* 5+ consecutive tokens we treat as branch with (n-1) / 2 leaf-value pairs,
				* if the number is odd, the last leaf has no value.
				*/
				newnode = createBarserNode(dict, head, BP_NODE_BRANCH, tokenstack[0]);
				newnode->flags |= BP_QUOTED_NAME & quotestack[0];

				BarserNode *tmphead = newnode;

				for(int i = 1; i < state.tokenCount; i++) {

				    newnode = createBarserNode(dict, tmphead, BP_NODE_LEAF, tokenstack[i]);
				    newnode->flags |= BP_QUOTED_NAME & quotestack[i];

				    if(++i < state.tokenCount) {
					newnode->value = strdup(tokenstack[i]);
					newnode->flags |= BP_QUOTED_VALUE & quotestack[i];
				    }
				}

			    }

			    break;
		    }
		}

		/* aftermath of the previous fall-through */
		if(state.parseEvent == BP_END_BLOCK) {
		    /* WOOP WOOP, WIND SHEAR, BANK ANGLE, PULL UP, TOO LOW, TERRAIN, TERRAIN */
		    if(PST_EMPTY(nodestack)) {
			state.parseEvent = BP_ERROR;
			state.parseError = BP_PERROR_LEVEL;
			break;
		    }
		    head = PST_POP(nodestack);
		}

		tokencleanup();
		break;

	    /* array start block i.e. '[' */
	    case BP_GOT_ARRAY:

		/* handle nested arrays - same case as GOT_BLOCK in an array */
		if(head->type == BP_NODE_ARRAY) {

		    /* first insert any existing tokens as array leaves */
		    for(int i = 0; i < state.tokenCount; i++) {
			newnode = createBarserNode(dict, head, BP_NODE_LEAF, "");
			newnode->value = strdup(tokenstack[i]);
			newnode->flags |= BP_QUOTED_VALUE & quotestack[i];
		    }

		    /* now enter into an unnamed array which is a new member of the upper array */
		    PST_PUSH_GROW(nodestack, head); /* save current position */
		    newnode = createBarserNode(dict, head, BP_NODE_ARRAY, "");
		    head = newnode;

		} else {

		    switch(state.tokenCount) {
			case 1:
			    PST_PUSH_GROW(nodestack, head);
			    newnode = createBarserNode(dict, head, BP_NODE_ARRAY, tokenstack[0]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[0];
			    head = newnode;
			    break;
			case 2:
			    PST_PUSH_GROW(nodestack, head);
			    newnode = createBarserNode(dict, head, BP_NODE_BRANCH, tokenstack[0]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[0];
			    newnode = createBarserNode(dict, newnode, BP_NODE_ARRAY, tokenstack[1]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[1];
			    head = newnode;
			    break;
			case 3:
			    PST_PUSH_GROW(nodestack, head);
			    newnode = createBarserNode(dict, head, BP_NODE_COLLECTION, tokenstack[0]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[0];
			    newnode = createBarserNode(dict, newnode, BP_NODE_BRANCH, tokenstack[1]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[1];
			    newnode = createBarserNode(dict, newnode, BP_NODE_ARRAY, tokenstack[2]);
			    newnode->flags |= BP_QUOTED_NAME & quotestack[2];
			    head = newnode;
			    break;
			/* unnamed aray?  nope. */
			case 0:
			    state.parseEvent = BP_ERROR;
			    state.parseError = BP_PERROR_EXP_ID;
			    break;
			default:
			    break;
		    }

		}

		tokencleanup();
		break;

	    /* array end block i.e. ']' */
	    case BP_END_ARRAY:

		if(head->type == BP_NODE_ARRAY) {
		    /*
		    * We allow some flexibility when constructing arrays. If we reach the end of an array,
		    * any leftover tokens are added as array leaves. This means that an array can be defined
		    * as a list of whitespace-separated tokens.
		    */
		    for(int i = 0; i < state.tokenCount; i++) {
			newnode = createBarserNode(dict, head, BP_NODE_LEAF, "");
			newnode->value = strdup(tokenstack[i]);
			newnode->flags |= BP_QUOTED_VALUE & quotestack[i];
		    }
		/* end of arRAY OUTSIDE AN ARRAY?! What are you, some kind of an animal?! */
		} else {
		    state.parseEvent = BP_ERROR;
		    state.parseError = BP_PERROR_BLOCK;
		}

		/* WOOP WOOP, WIND SHEAR, BANK ANGLE, PULL UP, TOO LOW, TERRAIN, TERRAIN */
		if(PST_EMPTY(nodestack)) {
		    state.parseEvent = BP_ERROR;
		    state.parseError = BP_PERROR_LEVEL;
		    break;
		}
		/* return to last branching point */
		head = PST_POP(nodestack);

		tokencleanup();
		break;

	    case BP_GOT_EOF:
	    case BP_NONE:
	    case BP_ERROR:
	    default:
		break;
	}

    }

    if(state.parseEvent != BP_ERROR && head != dict->root) {
	state.parseEvent = BP_ERROR;
	state.parseError = BP_PERROR_LEVEL;
    }

    PST_FREEDATA(tokenstack);
    PST_FREE(tokenstack);
    PST_FREE(nodestack);
    DST_FREE(quotestack);

    return state;

}
