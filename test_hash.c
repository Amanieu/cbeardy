#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash.h"

struct entry {
	struct entry *next;
	char string[0];
};

int main(int argc, char **argv)
{
	if (argc < 2) {
		printf("Usage: %s tablesize\n", argv[0]);
		printf("Finds collisions for a hash table using lines of standard input.\n");
		return 1;
	}

	int table_size = strtol(argv[1], NULL, 0);
	if (table_size < 1 || (table_size & (table_size - 1))) {
		printf("Table size must be a power of 2 greater than 0.\n");
		return 1;
	}

	struct entry **table = calloc(1, table_size * sizeof(struct entry *));

	int num_entries = 0;
	char buffer[1024];
	while (fgets(buffer, sizeof(buffer), stdin)) {
		if (buffer[strlen(buffer) - 1] == '\n')
			buffer[strlen(buffer) - 1] = '\0';
		if (!buffer[0])
			continue;
		int hash = hash_string(buffer) & (table_size - 1);
		struct entry *current = malloc(sizeof(struct entry) + strlen(buffer) + 1);
		strcpy(current->string, buffer);
		current->next = table[hash];
		table[hash] = current;
		num_entries++;
	}

	int i;
	int max_depth = 0;
	int num_collisions = 0;
	for (i = 0; i < table_size; i++) {
		int depth = 0;
		struct entry *current;
		for (current = table[i]; current; current = current->next)
			depth++;
		if (depth > max_depth)
			max_depth = depth;
		if (depth > 1) {
			num_collisions += depth - 1;
			/*printf("%d ", depth);
			for (current = table[i]; current; current = current->next)
				printf("%s ", current->string);
			printf("\n");*/
		}
	}

	printf("Load factor: %d / %d = %f\n", num_entries, table_size, (float)num_entries / table_size);
	printf("Max depth: %d\n", max_depth);
	printf("Total collisions: %d\n", num_collisions);

	return 0;
}
