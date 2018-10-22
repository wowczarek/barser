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

#include "xalloc.h"
#include "barser.h"

/* basic duration measurement macros */
#define DUR_INIT(name) unsigned long long name##_delta; struct timespec name##_t1, name##_t2;
#define DUR_START(name) clock_gettime(CLOCK_MONOTONIC,&name##_t1);
#define DUR_END(name) clock_gettime(CLOCK_MONOTONIC,&name##_t2); name##_delta = (name##_t2.tv_sec * 1000000000 + name##_t2.tv_nsec) - (name##_t1.tv_sec * 1000000000 + name##_t1.tv_nsec);
#define DUR_PRINT(name, msg) fprintf(stderr, "%s: %llu ns\n", msg, name##_delta);
#define DUR_EPRINT(name, msg) DUR_END(name); fprintf(stderr, "%s: %llu ns\n", msg, name##_delta);

#define QUERYCOUNT 10000

struct sample {
    BsNode* node;
    bool required;
};

/* generate a Fisher-Yates shuffled array of n uint32s */
static uint32_t* randArrayU32(const int count) {

    int i;
    struct timeval t;

    /* good idea oh Lord */
    gettimeofday(&t, NULL);
    /* of course it's a good idea! */
    srand(t.tv_sec + t.tv_usec);

    uint32_t* ret;
    xmalloc(ret, count * sizeof(uint32_t));

    for(i = 0; i < count; i++) {
	ret[i] = i;
    }

    for(i = 0; i < count; i++) {
	uint32_t j = i + rand() % (count - i);
	uint32_t tmp = ret[j];
	ret[j] = ret[i];
	ret[i] = tmp;
    }

    return ret;
}

static void* countcb(BsDict *dict, BsNode *node, void* user, void* feedback, bool* cont) {

    uint32_t* counter = user;
    struct sample* samples = feedback;

    if(samples[*counter].required) {
	samples[*counter].node = node;
    }

    *counter = *counter + 1;

    return samples;

}

int main(int argc, char **argv) {

    DUR_INIT(test);
    char* buf = NULL;
    char* qry = NULL;
    size_t len;
    int ret = 0;
    size_t nodecount;

    if(argc < 2) {

	fprintf(stderr, "Error: no arguments given.\n\nUsage: %s <filename> [-p] [\"path/to/node\"]\n\nfilename\tread input from file or stdin (\"-\")\n-p\t\tdump parsed contents to stdout\npath/to/node\tretrieve contents of node with given path\n\n",
		argv[0]);

	return -1;

    }

//    BsDict *dict = bsCreate("test", BS_NOINDEX);
    BsDict *dict = bsCreate("test", BS_NONE);

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
#ifdef COLL_DEBUG
    fprintf(stderr, "Total index collisions %d, max per node %d\n", dict->collcount, dict->maxcoll);
#endif /* COLL_DEBUG */
    nodecount = dict->nodecount;

    if(state.parseError) {

	bsPrintError(&state);
	return -1;

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
	fprintf(stderr, "Testing single fetch of \"%s\" from dictionary...", qry);
	fflush(stderr);
	DUR_START(test);
	BsNode* node = bsGet(dict, qry);
	DUR_END(test);
	fprintf(stderr, "done.\n");
	fprintf(stderr, "Single / first fetch took %llu ns\n", test_delta);



	if(node != NULL) {
	    fprintf(stderr, "\nNode found, hash of path \"%s\" is: 0x%08x, node name \"%s\":\n\n", qry, node->hash, node->name);
	    bsDumpNode(stdout, node);
	    printf("\n");
	} else {
	    fprintf(stderr, "\nNothing found for path \"%s\"\n\n", qry);
	    ret = 2;
	}

	uint32_t querycount = min(QUERYCOUNT, dict->nodecount);

	struct sample* samples;
	xcalloc(samples, dict->nodecount, sizeof(struct sample));
	char* paths[querycount];

	fprintf(stderr, "Extracting random %d nodes... ", querycount);

	uint32_t* sarr = randArrayU32(dict->nodecount);

	for(int i = 0; i < querycount; i++) {
	    samples[sarr[i]].required = true;
	}

	uint32_t  n = 0;
	bsNodeWalk(dict, dict->root, &n, samples, countcb);

	for(int i = 0; i < querycount; i++) {
	    if(samples[sarr[i]].node != NULL) {
		size_t pl = bsGetPath(samples[sarr[i]].node, NULL, 0);
		xmalloc(paths[i], pl);
		bsGetPath(samples[sarr[i]].node, paths[i], pl);
	    }
	}

	fprintf(stderr, "done.\n");

	fprintf(stderr, "Getting %d random paths from dictionary... ", querycount);
	fflush(stderr);
	int found = 0;
	DUR_START(test);
	for(int i = 0; i< querycount; i++) {
	    node = bsGet(dict, paths[i]);
	    if(node != NULL) {
		found++;
	    }
	}
	DUR_END(test);
	fprintf(stderr, "done.\n");
	fprintf(stderr, "Found %d out of %d, average fetch time %llu ns per query\n", found, querycount, test_delta / querycount);

	fprintf(stderr, "Freeing test data... ");
	fflush(stderr);

	for(int i = 0; i< querycount; i++) {
	    free(paths[i]);
	}
	free(samples);
	free(sarr);
	fprintf(stderr, "done.\n");

    /* no query given - test duplication */
    } else {

	fprintf(stderr, "Duplicating dictionary... ");
	fflush(stderr);
	DUR_START(test);
	BsDict* dup = bsDuplicate(dict, "newdict", dict->flags);
	DUR_END(test);

	fprintf(stderr, "done.\n");
	fprintf(stderr, "Duplicated in %llu ns, %zu nodes, %.0f nodes/s\n",
		test_delta, nodecount, (1000000000.0 / test_delta) * dup->nodecount);

	fprintf(stderr, "Freeing duplicate... ");
	fflush(stderr);

	DUR_START(test);
	bsFree(dup);
	DUR_END(test);

	fprintf(stderr, "done.\n");
	fprintf(stderr, "Freed in %llu ns, %zu nodes, %.0f nodes/s\n",
		test_delta, nodecount, (1000000000.0 / test_delta) * nodecount);
    }

    fprintf(stderr, "Freeing dictionary... ");
    fflush(stderr);

    DUR_START(test);
    bsFree(dict);
    DUR_END(test);

    fprintf(stderr, "done.\n");
    fprintf(stderr, "Freed in %llu ns, %zu nodes, %.0f nodes/s\n",
		test_delta, nodecount, (1000000000.0 / test_delta) * nodecount);


    free(buf);

    return ret;

}
