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
 * @file   rbt_display.c
 * @date   Fri Sep 14 23:27:00 2018
 *
 * @brief  ASCII red-black tree display code, separate from core rbt.c
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rbt_display.h"
#include "fq.h"
#include "rbt.h"

/* get index of character at position x,y in a maxwidth * maxheight buffer with newlines every maxwidth */
static int getPos(const int x, const int y, const int maxwidth, const int maxheight) {

    int ret = y * (maxwidth + 1) + x;

    if(y >= maxheight) return -1;
    if(x >= maxwidth) return -1;
    if (ret >= ((maxwidth + 1) * maxheight)) return -1;

    return ret;

}

/* put string at position x,y into a buffer representing a maxwidth * maxheight rectangle */
static void putPos(char *buf, const char *src, const int x, const int y, const int maxwidth, const int maxheight) {

    int sl = strlen(src);
    int pos = getPos(x, y, maxwidth, maxheight);
    int maxpos = (maxwidth + 1) * maxheight;

    if(pos == -1) {
	return;
    }

    if((pos + sl) > maxpos) {
	sl = maxpos - (pos + sl);
    }

    if(sl > 0) {
	memcpy(buf + pos, src, ((x + sl) > maxwidth) ? maxwidth - x : sl);
    }
}

/* display node in a maxwidth * maxheight char array */
static void rbDisplayNode(RbNode *node, char *buf, const int x, const int y, const int maxwidth, const int maxheight, const bool showNull) {

    char tmp[50];

    if(node == NULL) {
	if(showNull) {
	    snprintf(tmp, 50, "BX");
	    putPos(buf, tmp, x, y, maxwidth, maxheight);
	}
    } else {
	snprintf(tmp, 50, "%s%u", node->red ? "R" : "B", node->key);
	putPos(buf, tmp, x, y, maxwidth, maxheight);
    }

}

/* retutn pointer to a dynamically allocated buffer containing an ASCII dump of a red-black tree using a maxwidth * maxheight text block, optionally showing leafs */
char* rbDisplay(RbTree *tree, const int maxwidth, const int maxheight, const bool showNull) {

    int maxpos = (maxwidth + 1) * maxheight;
    char* obuf = malloc(maxpos + 1);

    struct nodepos {
	RbNode *node;
	int x;
	int y;
	int level;
    };

    if(obuf == NULL) {
	return NULL;
    }

    struct nodepos current = { tree->root, maxwidth / 2, 1, 2 };
    struct nodepos tmp = current;
    DFQueue *queue = dfqCreate(16, sizeof(struct nodepos), FQ_NONE);

    memset(obuf, '.', maxpos);
    obuf[maxpos] = '\0';

    for(int i = 0; i < maxheight; i++) {
	obuf[(maxwidth + 1) * i] = '\n';
    }

    dfqPush(queue, &current);

    while(!queue->empty) {

	current = *(struct nodepos*)dfqPop(queue);

	if (current.node != NULL) {

	    tmp.y = current.y + 2;
	    tmp.level = current.level + 1;

	    tmp.node = current.node->children[RB_LEFT];
	    tmp.x = current.x - (maxwidth >> current.level);
	    dfqPush(queue, &tmp);

	    tmp.node = current.node->children[RB_RIGHT];
	    tmp.x = current.x + (maxwidth >> current.level);
	    dfqPush(queue, &tmp);
	};

	rbDisplayNode(current.node, obuf, current.x, current.y, maxwidth, maxheight, showNull);

    }

    dfqFree(queue);

    return obuf;

}

/* dump tree contents in-order */
void rbDumpInOrder(RbTree *tree, const int dir) {

    rbInOrder(tree, rbDumpCallback, NULL, dir);

}

/* dump tree contents breadth-first */
void rbDumpBreadthFirst(RbTree *tree, const int dir) {

    rbBreadthFirst(tree, rbDumpCallback, NULL, dir);

}
