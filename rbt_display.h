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
 * @file   rbt_display.h
 * @date   Fri Sep 14 23:27:00 2018
 *
 * @brief  red-black tree display function declarations
 *
 */

#ifndef RBT_DISPLAY_H_
#define RBT_DISPLAY_H_

#include "rbt.h"
#include <stdbool.h>

/* constants */

/* show / hide NULL in tree display */
#define RB_SHOW_NULL true
#define RB_NO_NULL false

/* return pointer to a dynamically allocated buffer containing an ASCII dump of tree hierarchy in a maxwidth x maxheight text block */
char*		rbDisplay(RbTree *tree, const int maxwidth, const int maxheight, const bool showNull);

/* dump tree contents in-order, dir RB_ASC | RB_DESC */
void		rbDumpInOrder(RbTree *tree, const int dir);

/* dump tree contents breadth-first (level by level), dir RB_ASC = left to right | RB_DESC = right to left */
void		rbDumpBreadthFirst(RbTree *tree, const int dir);

#endif /* RBT_DISPLAY_H_ */
