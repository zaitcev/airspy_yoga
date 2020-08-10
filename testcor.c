/*
 * Test of the correlation
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yoga.h"

#define TAG "testcor"

static struct rstate rs;

static void Usage(void) {
	fprintf(stderr, "Usage: test_cor datafile\n");
	exit(1);
}

int main(int argc, char **argv) {
	char *input_name;
	FILE *ifp;
	char line[80], *nump, *endp;
	long value;
	int cv;

	if (argc != 2) {
		Usage();
	}
	input_name = argv[1];
	if (input_name[0] == '-') {
		if (input_name[1] != 0) {
			Usage();
		}
		ifp = stdin;
	} else {
		ifp = fopen(input_name, "r");
		if (ifp == NULL) {
			fprintf(stderr, TAG ": Cannot open %s: %s\n",
			    input_name, strerror(errno));
			exit(1);
		}
	}

	while (fgets(line, 80, ifp) != NULL) {
		nump = strtok(line, " \t\n");
		if (nump == NULL)
			continue;
		value = strtol(nump, &endp, 10);
		if (endp == nump || *endp != 0) {
			fprintf(stderr, TAG ": Invalid number: %s\n", nump);
			continue;
		}
		if (value < -2048 || value >= 2048) {
			fprintf(stderr, TAG ": Invalid value: %ld\n", value);
			continue;
		}
		cv = preamble_match(&rs, value);
		if (cv == -1)
			continue;
		printf("%d\n", cv);
	}

	return 0;
}
