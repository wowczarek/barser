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
 * @file   barser_test.c
 * @date   Fri Sep 14 23:27:00 2018
 *
 * @brief  barser implementation test code
 *
 */

#define _POSIX_C_SOURCE 199309L /* because clock_gettime */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>

#include "xxh.h"

#include "barser.h"

/* basic duration measurement macros */
#define DUR_INIT(name) unsigned long long name##_delta; struct timespec name##_t1, name##_t2;
#define DUR_START(name) clock_gettime(CLOCK_MONOTONIC,&name##_t1);
#define DUR_END(name) clock_gettime(CLOCK_MONOTONIC,&name##_t2); name##_delta = (name##_t2.tv_sec * 1000000000 + name##_t2.tv_nsec) - (name##_t1.tv_sec * 1000000000 + name##_t1.tv_nsec);
#define DUR_PRINT(name, msg) fprintf(stderr, "%s: %llu ns\n", msg, name##_delta);
#define DUR_EPRINT(name, msg) DUR_END(name); fprintf(stderr, "%s: %llu ns\n", msg, name##_delta);

#include "itoa.h"

int main(int argc, char **argv) {

    DUR_INIT(test);
    char* buf = NULL;
    char* qry = NULL;
    size_t len;

    if(argc < 2) {

	fprintf(stderr, "Error: no arguments given.\n\nUsage: %s <filename> [-p] [\"path/to/node\"]\n\n<filename>\tread input from file or stdin (\"-\")\n-p\t\tdump parsed contents to stdout\npath/to/node\tretrieve contents of node with given path\n\n",
		argv[0]);

	return -1;

    }

    BsDict *dict = bsCreate("test");

    fprintf(stderr, "Loading \"%s\" into memory... ", argv[1]);
    fflush(stderr);

    DUR_START(test);
    len = getFileBuf(&buf, argv[1]);

    if(len <= 0 || buf == NULL) {
	fprintf(stderr, "Error: could not read input file\n");
	return -1;
    }

    DUR_END(test);
    fprintf(stderr, "done.\n");

    fprintf(stderr, "Loaded %zu bytes in %llu ns, %.03f MB/s\n",
		len, test_delta, (1000000000.0 / test_delta) * (len / 1000000.0));

    fprintf(stderr, "Parsing data... ");
    fflush(stderr);

    DUR_START(test);
    BsState state = bsParse(dict, buf, len);
    DUR_END(test);

    fprintf(stderr, "done.\n");
    fprintf(stderr, "Parsed in %llu ns, %.03f MB/s, %zu nodes, %.0f nodes/s\n",
		test_delta, (1000000000.0 / test_delta) * (len / 1000000.0),
		dict->nodecount, (1000000000.0 / test_delta) * dict->nodecount);
    fprintf(stderr, "Total index collisions %d, max per node %d\n", dict->collcount, dict->maxcoll);


    if(state.parseError) {

	bsPrintError(&state);

    } else if(argc >= 3) {

	if(!strcmp(argv[2], "-p")) {
	    bsDump(stdout, dict);
	    if(argc >= 4) {
		qry = argv[3];
	    }
	} else {
		qry = argv[2];
	}
    }

    if(qry != NULL) {
	BsNode* node = bsQuery(dict, qry);
	if(node != NULL) {
	    printf("Node found, hash of query \"%s\" is: 0x%08x, node name \"%s\":\n", qry, node->hash, node->name);
	    bsDumpNode(stdout, node);
	} else {
	    printf("Nothing found for query \"%s\"\n", qry);
	}
    }

    bsFree(dict);

    free(buf);

    return 0;

}