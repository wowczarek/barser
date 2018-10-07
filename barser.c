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
#include <sys/stat.h>
#include <unistd.h>
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

/* parser state machine */
enum {
    BP_GET_NAME = 0,		/* awaiting identifier */
    BP_GET_VALUE,		/* awaiting value */
    BP_GOT_VALUE		/* got value, awaiting children or end of entry */
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

/* initialise parser state */
static inline void initBarserState(BarserState *state, char* buf, const size_t bufsize) {

    state->current = buf;
    state->prev = '\0';
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

    state->parseState = BP_GET_NAME;
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
	} else printf("not advancing\n");

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

    struct stat fst;
    char *buf = NULL;
    FILE *fl = fopen(fileName, "r");

    if(fl == NULL) {
	*out = NULL;
	return 0;
    }

    int flfd = fileno(fl);
    memset(&fst, 0, sizeof(struct stat));
    if(fstat(flfd, &fst) < 0) {
	return -1;
    }

    buf = malloc(fst.st_size + 1);

    if(buf == NULL) {
	*out = NULL;
	return 0;
    }

    size_t ret = fread(buf, 1, fst.st_size, fl);
    if(ret < fst.st_size) {

	if(feof(fl)) {
	    fprintf(stderr, "Error: Truncated data while reading '%s': file size %zu bytes, read %zu bytes\n", fileName, fst.st_size, ret);
	}

	if(ferror(fl)) {
	    fprintf(stderr, "Error while reading '%s': file size %zu bytes, read %zu bytes\n", fileName, fst.st_size, ret);
	}

	free(buf);
	return 0;

    }
    buf[fst.st_size] = EOF;

    fclose(fl);

    *out = buf;

    return(fst.st_size + 1);
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


void
dumpBarserNode(BarserNode *node, int level)
{
    char memberSepChar = '\0';
    char endValueChar = ';';
    char memberEndChar = '\0';
    bool noIndentArray = true;

/*
    char memberSepChar = BP_ARRAYSEP_CHAR;
    char endValueChar = BP_ENDVAL_CHAR;
*/

    /* allocate enough indentation space for current level + 1 */
    int maxwidth = (level + 1) * BP_INDENT_WIDTH;
    char indent[ maxwidth + 1];
    BarserNode *n = NULL;
    bool inArray = (node->parent != NULL && node->parent->type == BP_NODE_ARRAY);
    bool isArray = (node->type == BP_NODE_ARRAY);
    bool hadBranchSibling = inArray && node->_prev != NULL && node->_prev->type != BP_NODE_LEAF;

    /* fill up the buffer with indent char, but up to current level only */
    memset(indent, BP_INDENT_CHAR, maxwidth);
    memset(indent + level * BP_INDENT_WIDTH, '\0', BP_INDENT_WIDTH);
    /* yessir... */
    indent[maxwidth] = '\0';

    printf("%s%s", inArray && noIndentArray && !hadBranchSibling ? " " : indent, inArray ? "" : node->name);

    if(node->childCount == 0) {
	if(node->type != BP_NODE_ROOT) {
	if(node->value && strlen(node->value)) {
	    if(node->quotedValue) {
	    printf("%c%c%s%c%c%c", inArray ? '\0' : ' ', BP_DBLQUOTE_CHAR,
			    node->value, BP_DBLQUOTE_CHAR,
			    inArray ? memberSepChar : BP_ENDVAL_CHAR,
			    (inArray && noIndentArray) ? memberEndChar : '\n');
	    } else {
	    printf("%c%s%c%c", inArray ? '\0' : ' ', node->value,
			    inArray ? memberSepChar : BP_ENDVAL_CHAR,
			    (inArray && noIndentArray) ? memberEndChar : '\n');

	    }
	
	} else {
	    printf("%c%c", inArray ? memberSepChar : BP_ENDVAL_CHAR,
	    (inArray && noIndentArray) ? memberEndChar : '\n');
	}
	}
    } else {
	if(node->type != BP_NODE_ROOT) {
	printf( "%s%c%c", strlen(node->name) ? " " : "",
	    isArray ? BP_STARTARRAY_CHAR : BP_STARTBLOCK_CHAR,
	    isArray && noIndentArray ? '\0' : '\n');
	} else {
	    printf("\n");
	}
	/* increase indent in case if we want to print something here later */
	memset(indent + level * BP_INDENT_WIDTH, BP_INDENT_CHAR, BP_INDENT_WIDTH);
	LL_FOREACH_DYNAMIC(node, n) {
//	    printCckConfigNode(n, node->type == BP_NODE_ROOT ? level : (level + 1));
	    dumpBarserNode(n, level + 1);
        }
	/* decrease indent again */
	memset(indent + (level) * BP_INDENT_WIDTH, '\0', BP_INDENT_WIDTH);
	if(node->type != BP_NODE_ROOT) {
	    printf("%s%c%c\n", isArray && noIndentArray ? " " : indent,
		isArray ? BP_ENDARRAY_CHAR : BP_ENDBLOCK_CHAR,
		inArray ? memberSepChar : endValueChar);
	} else {
	    printf("\n");
	}
    }
//    printf("%snode name %s, child count: %d\n", indent, node->name, node->childCount);
//	printf("%s child #%d: ", indent, ++childCount);


}
/*
void
dumpCckConfigNode(CckConfigNode *node)
{

    if(node == NULL) {
	return;
    }

    printCckConfigNode(node, node->type == BP_NODE_ROOT ? -1 : 0);
}
*/
void
freeBarserNode(BarserNode *node)
{

    if(node == NULL) {
	return;
    }

    free(node->name);
    if(node->value != NULL) {
	free(node->value);
    }

}

/* create a new node in @dict, attached to @parent, with name @name */
BarserNode*
createBarserNode(BarserDict *dict, BarserNode *parent, const char* name)
{

    BarserNode *ret;

    if(dict == NULL) {
	return NULL;
    }

    ret = calloc(1, sizeof(BarserNode));

    if(ret == NULL) {
	return NULL;
    }

    ret->parent = parent;

    if(parent != NULL) {
	/* if we are adding an array member, call it by number, ignoring the name */
	if(parent->type == BP_NODE_ARRAY) {
	    tmpstr(myName, INT_STRSIZE);
	    snprintf(myName, INT_STRSIZE, "%d", parent->childCount);
	    ret->name = strdup(myName);
	} else {
	    if(name == NULL) {
		goto onerror;
	    }
	    ret->name = strdup(name);
	}

	ret->_pathLen = parent->_pathLen + strlen(name) + 1;
    } else {
	ret->name = strdup("");
    }

    if(parent != NULL) {
	LL_APPEND_DYNAMIC(parent, ret);
	parent->childCount++;
    } else {
	if(dict->root != NULL) {
	    printf("Error: will not replace dictionary '%s' root with node '%s'\n", dict->name, name);
	    goto onerror;
	} else {
	    dict->root = ret;
	    ret->type = BP_NODE_ROOT;
	}
    }

    ret->dict = dict;
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
    createBarserNode(ret, NULL, "");
    return ret;
}

void
freeBarserDict(BarserDict *dict) {

    if(dict == NULL) {
	return;
    }

    deleteBarserNode(dict, dict->root);

    free(dict->name);
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
			state->linestart = state->slinestart;
			state->lineno = state->slineno;
			state->linepos = state->slinepos;
			break;
		    case BP_SKIP_MLCOMMENT:
			fprintf(stderr, "Unterminated multiline comment");
			state->linestart = state->slinestart;
			state->lineno = state->slineno;
			state->linepos = state->slinepos;
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
			printf("mlc %zd\n", state->linepos);
			state->slinestart = state->linestart;
			state->slineno = state->lineno;
			state->slinepos = state->linepos;
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
		    memcpy(state->str, state->strstart, state->current - state->strstart);
		}
		/* raise a "got token" event */
		state->parseEvent = BP_GOT_TOKEN;
		state->scanState = BP_SKIP_WHITESPACE;
		break;

	    case BP_GET_QUOTED:
		state->str = malloc(1);
		size_t ssize = 0;
		bool captured = false;
		while(c != qchar) {
		    if(c == BP_ESCAPE_CHAR) {
			c = barserForward(state);
			switch(c) {
			    case 't':
				state->str[ssize] = '\t';
				captured = true;
				break;
			    case 'n':
				state->str[ssize] = '\n';
				captured = true;
				break;
			    case BP_ESCAPE_CHAR:
				state->str[ssize] = BP_ESCAPE_CHAR;
				captured = true;
				break;
			    default:
				captured = false;
				break;
			}
		    }
		    if(captured) {
			captured = false;
		    } else {
			if(c == EOF) {
			    state->parseEvent = BP_ERROR;
			    state->parseError = BP_PERROR_EOF;
			    free(state->str);
			    return;
			}
			state->str[ssize] = c;
		    }
		    c = barserForward(state);
		    state->str = realloc(state->str, ++ssize + 1);
		}
		c = barserForward(state);
		state->str[ssize] = '\0';
		/* raise a "got quoted string" event */
		state->parseEvent = BP_GOT_QUOTED;
		state->scanState = BP_SKIP_WHITESPACE;
		break;

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
		    state->slinestart = state->linestart;
		    state->slineno = state->lineno;
		    state->slinepos = state->linepos;
		    /* because we can have two quotation mark types, we save the one we encountered */
		    qchar = c;
		    c = barserForward(state);
		    break;
		case BP_ENDVAL_CHAR:
		    state->scanState = BP_SKIP_WHITESPACE;
		    state->parseEvent = BP_GOT_ENDVAL;
		    c = barserForward(state);
		    break;
		case BP_STARTBLOCK_CHAR:
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

}


/* parse the contents of buf into dictionary dict, return last state */
BarserState barseBuffer(BarserDict *dict, char *buf, const size_t len) {

    PST_DECL(tokenstack, char*, 16);
    PST_DECL(nodestack, char*, 16);

    BarserNode *current;
    BarserState state;
    int c = 0;

    if(dict == NULL) {
	state.parseError = BP_PERROR_NULL;
	return state;
    }

    current = dict->root;
    PST_INIT(tokenstack);
    PST_INIT(nodestack);
    initBarserState(&state, buf, len);
    c = buf[0];

    /* keep parsing until no more data or parser error encountered */
    while(state.parseEvent != BP_GOT_EOF && !state.parseError) {

	state.parseEvent = BP_NONE;
	state.parseError = BP_PERROR_NONE;

	barserScan(&state);

	/* process parser event */
	switch(state.parseEvent) {
	    case BP_GOT_TOKEN:
		PST_PUSH_GROW(tokenstack, state.str);
		state.tokenCount++;
//		printf("got token '%s'\n", state.str);
		break;
	    case BP_GOT_QUOTED:
		PST_PUSH_GROW(tokenstack, state.str);
		state.tokenCount++;
//		printf("got quoted \"%s\"\n", state.str);
		break;
	    case BP_GOT_BLOCK:
#ifdef DUMP_TOKENS
		printf("Got %d tokens: ", tokenstack_sh);
		for(int i = 0; i < tokenstack_sh; i++) {
		    printf("'%s' ", tokenstack_st[i]);
		}
		printf("\n");
#endif
		PST_FREEDATA(tokenstack);
		state.tokenCount = 0;
//		printf("got block\n");
		break;
	    case BP_GOT_ENDVAL:
#ifdef DUMP_TOKENS
		printf("Got %d tokens: ", tokenstack_sh);
		for(int i = 0; i < tokenstack_sh; i++) {
		    printf("'%s' ", tokenstack_st[i]);
		}
		printf("\n");
#endif
		PST_FREEDATA(tokenstack);
		state.tokenCount = 0;
//		printf("got endval\n");
		break;
	    case BP_END_BLOCK:
//		printf("got end block\n");
			break;
	    case BP_GOT_ARRAY:
#ifdef DUMP_TOKENS
		printf("Got %d tokens: ", tokenstack_sh);
		for(int i = 0; i < tokenstack_sh; i++) {
		    printf("'%s' ", tokenstack_st[i]);
		}
		printf("\n");
#endif
		PST_FREEDATA(tokenstack);
		state.tokenCount = 0;
//		printf("got array\n");
		break;
	    case BP_END_ARRAY:
//		printf("got endarray\n");
		break;
	    case BP_GOT_EOF:
//		printf("got EOF\n");
		break;
	    case BP_NONE:
	    case BP_ERROR:
	    default:
		break;
	}

    }
/*
    if(parseEvent != BP_ERROR && currNode != dict->root) {
	parseEvent = BP_ERROR;
	parseError = BP_PERROR_LEVEL;
    }
*/
//	dumpCckConfigNode(dict->root);

//	dumpCckConfigNode(dict->root);
    PST_FREEDATA(tokenstack);
    PST_FREE(tokenstack);
    PST_FREE(nodestack);
    return state;

}
