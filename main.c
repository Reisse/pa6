#include "framework.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char * argv[])
{
	if (!argc) {
		// well...
		fprintf(stderr, "argc == 0, really?\n");
		return 0xDEAD;
	}

	int p_count_pos = 0;
	bool mutexcl = false;
	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "-p") == 0) {
			p_count_pos = i + 1;
		} else if (strcmp(argv[i], "--mutexl") == 0) {
			mutexcl = true;
		}
	}

	if (!p_count_pos || !(p_count_pos < argc)) {
		fprintf(stderr, "%s: Usage: %s -p <N> [--mutexl]\n", argv[0], argv[0]);
		return -1;
	}

	char * remainder = NULL;
	long process_count = strtol(argv[p_count_pos], &remainder, 10);
	if (*argv[p_count_pos] == '\0' || *remainder != '\0') {
		fprintf(stderr, "%s: Can't parse %s\n", argv[0], argv[p_count_pos]);
		return -1;
	} else if (process_count < 1) {
		fprintf(stderr, "%s: Process count should be greater than zero\n", argv[0]);
		return -1;
	} else if (process_count > 127 /* int8_t max */) {
		fprintf(stderr, "%s: Process count should be lesser than 128\n", argv[0]);
		return -1;
	}

	return run_distributed_system(process_count, mutexcl);
}
