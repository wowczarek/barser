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
 * @file   xalloc.h
 * @date   Sat Jan 9 16:14:10 2018
 *
 * @brief  Wrapper macros for malloc / calloc / realloc that exit on NULL
 *
 */

#ifndef XALLOC_H_
#define XALLOC_H_

#include <stdlib.h>
#include <stdio.h>

#ifndef EXIT_ALLOCERR
#define EXIT_ALLOCERR 255
#endif /* EXIT_ALLOCERR */

#ifndef xallocfail
#define xallocfail(comp) (fprintf(stderr, "*** xalloc: %s failure in %s (returned NULL), exiting. ***\n",\
				comp, __func__), exit(EXIT_ALLOCERR))
#endif /* xallocfail */

#ifndef xmalloc
#define xmalloc(var, size) ((((var) = malloc(size)) == NULL) ? xallocfail("malloc") : var)
#endif /* xmalloc */

#ifndef xcalloc
#define xcalloc(var, n, size) ((((var) = calloc(n, size)) == NULL) ? xallocfail("calloc") : var)
#endif /* xcalloc */

#ifndef xrealloc
#define xrealloc(var, ptr, size) ((((var) = realloc(ptr, size)) == NULL) ? xallocfail("realloc") : var)
#endif /* xrealloc */

#ifndef xfree
#define xfree(var) (void)((var) = (((var) != NULL) ? (free(var), NULL) : NULL))
#endif /* xfree */

#endif /* XALLOC_H_ */
