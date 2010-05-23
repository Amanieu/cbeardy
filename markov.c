#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <stdbool.h>
#include "hash.h"
#include "math.h"
#include "mempool.h"
#include "stringpool.h"
#include "markov.h"

// Size of the markov chain node hash table
#define MARKOV_TABLE_SIZE 0x1000000

// Size of start node hash table
#define MARKOV_START_SIZE 0x200000

// An exit for a node in a markov chain
struct markov_node_t;
struct markov_exit_t {
	struct markov_node_t *node;
	int count;
};

// An entry in an exit hash table
struct markov_hash_exit_t {
	struct markov_hash_exit_t *next;
	struct markov_node_t *node;
	int count;
};

// A node in a markov chain
struct markov_node_t {
	struct markov_node_t *next;
	union {
		const char *strings[MARKOV_ORDER];
		markov_offset_t offset;
	};
	int num_exits;
	union {
		struct markov_exit_t *exits;
		struct markov_hash_exit_t **hashtable;
	};
};

// Hash table of markov chain nodes
static struct markov_node_t *markov_table[MARKOV_TABLE_SIZE];

// Hash table of start nodes
static struct markov_hash_exit_t *markov_start_table[MARKOV_START_SIZE];
static int markov_num_start;

// Memory pool for hash exit nodes
static struct mempool_t markov_hashexitpool;

// Memory pool for the node structure
static struct mempool_t markov_nodepool;

// Memory pools for exits with 1 to 16 elements
static struct mempool_t markov_exitpool_small[16];

// Memory pools for exits with 32, 64 and 128 elements
static struct mempool_t markov_exitpool_32;
static struct mempool_t markov_exitpool_64;
static struct mempool_t markov_exitpool_128;

// Statistics for malloc()-based allocations
static int markov_largepool_count;
static int markov_largepool_total;

// Search the hash table for a node
static inline struct markov_node_t *markov_find_node(int hash, const char *const *strings)
{
	struct markov_node_t *node;
	for (node = markov_table[hash]; node; node = node->next) {
		int i;
		for (i = 0; i < MARKOV_ORDER; i++) {
			if (node->strings[i] != strings[i])
				break;
		}

		if (i == MARKOV_ORDER)
			return node;
	}

	return NULL;
}

// Search the hash table for a node. Allocates a new node if one wasn't found.
// All strings should have been allocated using string_copy().
static inline struct markov_node_t *markov_get_node(const char *const *strings)
{
	int hash = hash_strings(MARKOV_ORDER, strings) & (MARKOV_TABLE_SIZE - 1);

	struct markov_node_t *node = markov_find_node(hash, strings);
	if (node)
		return node;

	// Allocate a new node
	node = mempool_alloc(&markov_nodepool, sizeof(struct markov_node_t));
	node->num_exits = 0;
	node->exits = NULL;
	int i;
	for (i = 0; i < MARKOV_ORDER; i++)
		node->strings[i] = strings[i];
	node->next = markov_table[hash];
	markov_table[hash] = node;
	return node;
}

// Search the node for the given exit and increments it if found. Returns false if not found.
static inline bool markov_increment_exit(struct markov_node_t *node, struct markov_node_t *exit)
{
	if (node->num_exits > 128) {
		int hash = hash_pointer(exit) & (next_power_of_2(node->num_exits) - 1);
		struct markov_hash_exit_t *current;
		for (current = node->hashtable[hash]; current; current = current->next) {
			if (current->node == exit) {
				current->count++;
				return true;
			}
		}
	} else {
		int i;
		for (i = 0; i < node->num_exits; i++) {
			if (node->exits[i].node == exit) {
				node->exits[i].count++;
				return true;
			}
		}
	}

	return false;
}

// Add an exit to a node
static inline void markov_add_exit(struct markov_node_t *node, struct markov_node_t *exit)
{
	// First see if we already have this exit
	if (markov_increment_exit(node, exit))
		return;

	// We need to add a new exit. See if we need to extend the exit list.
	if (node->num_exits <= 16 || is_power_of_2(node->num_exits)) {
		struct markov_exit_t *newexits;
		if (node->num_exits == 0) {
			newexits = mempool_alloc(&markov_exitpool_small[0], sizeof(struct markov_exit_t));
			node->exits = newexits;
		} else if (node->num_exits < 16) {
			struct mempool_t *oldpool = &markov_exitpool_small[node->num_exits - 1];
			struct mempool_t *newpool = &markov_exitpool_small[node->num_exits];
			newexits = mempool_alloc(newpool, sizeof(struct markov_exit_t) * (node->num_exits + 1));
			memcpy(newexits, node->exits, sizeof(struct markov_exit_t) * node->num_exits);
			mempool_free(oldpool, node->exits);
			node->exits = newexits;
		} else if (node->num_exits == 16) {
			newexits = mempool_alloc(&markov_exitpool_32, sizeof(struct markov_exit_t) * 32);
			memcpy(newexits, node->exits, sizeof(struct markov_exit_t) * 16);
			mempool_free(&markov_exitpool_small[15], node->exits);
			node->exits = newexits;
		} else if (node->num_exits == 32) {
			newexits = mempool_alloc(&markov_exitpool_64, sizeof(struct markov_exit_t) * 64);
			memcpy(newexits, node->exits, sizeof(struct markov_exit_t) * 32);
			mempool_free(&markov_exitpool_32, node->exits);
			node->exits = newexits;
		} else if (node->num_exits == 64) {
			newexits = mempool_alloc(&markov_exitpool_128, sizeof(struct markov_exit_t) * 128);
			memcpy(newexits, node->exits, sizeof(struct markov_exit_t) * 64);
			mempool_free(&markov_exitpool_64, node->exits);
			node->exits = newexits;
		} else if (node->num_exits == 128) {
			newexits = node->exits;
			node->hashtable = calloc(1, sizeof(struct markov_hash_exit_t *) * 256);
			assert(node->hashtable);
			int i;
			for (i = 0; i < 128; i++) {
				int hash = hash_pointer(newexits[i].node) & 127;
				struct markov_hash_exit_t *current = mempool_alloc(&markov_hashexitpool, sizeof(struct markov_hash_exit_t));
				current->node = newexits[i].node;
				current->count = newexits[i].count;
				current->next = node->hashtable[hash];
				node->hashtable[hash] = current;
			}
			mempool_free(&markov_exitpool_128, newexits);
			markov_largepool_count++;
			markov_largepool_total += 256;
		} else {
			node->hashtable = realloc(node->hashtable, sizeof(struct markov_hash_exit_t *) * node->num_exits * 2);
			memset(node->hashtable + node->num_exits, 0, sizeof(struct markov_hash_exit_t *) * node->num_exits);
			assert(node->hashtable);
			int i;
			for (i = 0; i < node->num_exits; i++) {
				struct markov_hash_exit_t **insert = &node->hashtable[i];
				struct markov_hash_exit_t *current = node->hashtable[i];
				while (current) {
					int hash = hash_pointer(current->node) & (node->num_exits * 2 - 1);
					if (hash != i) {
						*insert = current->next;
						current->next = node->hashtable[hash];
						node->hashtable[hash] = current;
						current = *insert;
					} else {
						insert = &current->next;
						current = *insert;
					}
				}
			}
			markov_largepool_total += node->num_exits + node->num_exits / 2;
		}
	}

	// Now finally add the exit
	if (++node->num_exits > 128) {
		int hash = hash_pointer(exit) & (next_power_of_2(node->num_exits) - 1);
		struct markov_hash_exit_t *current = mempool_alloc(&markov_hashexitpool, sizeof(struct markov_hash_exit_t));
		current->node = exit;
		current->count = 1;
		current->next = node->hashtable[hash];
		node->hashtable[hash] = current;
	} else {
		node->exits[node->num_exits - 1].node = exit;
		node->exits[node->num_exits - 1].count = 1;
	}
}

// Add a node to the start of the chain
static inline void markov_add_start(struct markov_node_t *node)
{
	int hash = hash_pointer(node) & (MARKOV_START_SIZE - 1);

	// Search the hash table for the node
	struct markov_hash_exit_t *start;
	for (start = markov_start_table[hash]; start; start = start->next) {
		if (start->node == node) {
			start->count++;
			return;
		}
	}

	// Allocate a new entry and add it to the hash table
	start = mempool_alloc(&markov_hashexitpool, sizeof(struct markov_hash_exit_t));
	start->count = 1;
	start->node = node;
	start->next = markov_start_table[hash];
	markov_start_table[hash] = start;
	markov_num_start++;
}

// Train the markov model using the given sentence. All strings in the sentence
// must have been allocated using string_copy().
static inline void markov_train(int length, const char *const *sentence)
{
	// Ignore empty sentences
	if (!length)
		return;

	// Handle sentences shorter than MARKOV_ORDER
	if (length < MARKOV_ORDER) {
		const char *buffer[MARKOV_ORDER];
		int i;
		for (i = 0; i < length; i++)
			buffer[i] = sentence[i];
		for (i = length; i < MARKOV_ORDER; i++)
			buffer[i] = NULL;
		markov_add_start(markov_get_node(buffer));
		return;
	}

	// Build first node
	struct markov_node_t *node = markov_get_node(sentence);
	markov_add_start(node);

	// Build middle nodes
	int i;
	for (i = MARKOV_ORDER; i < length; i++) {
		sentence++;
		struct markov_node_t *nextnode = markov_get_node(sentence);
		markov_add_exit(node, nextnode);
		node = nextnode;
	}

	// Build last node
	sentence++;
	const char *buffer[MARKOV_ORDER];
	for (i = 0; i < MARKOV_ORDER - 1; i++)
		buffer[i] = sentence[i];
	buffer[MARKOV_ORDER - 1] = NULL;
	struct markov_node_t *nextnode = markov_get_node(buffer);
	markov_add_exit(node, nextnode);
}

// Initialize various stuff
static inline void markov_init(void)
{
	string_init();
}

// Print the entire model
static inline void markov_print(void)
{
	// Print all start nodes
	printf("START\n");
	int i;
	for (i = 0; i < MARKOV_START_SIZE; i++) {
		struct markov_hash_exit_t *current;
		for (current = markov_start_table[i]; current; current = current->next) {
			printf("  %d ->", current->count);
			int j;
			for (j = 0; j < MARKOV_ORDER; j++)
				printf(" %s", current->node->strings[j]);
			printf("\n");
		}
	}

	// Print all the other nodes
	for (i = 0; i < MARKOV_TABLE_SIZE; i++) {
		struct markov_node_t *current;
		for (current = markov_table[i]; current; current = current->next) {
			printf("NODE");
			int j;
			for (j = 0; j < MARKOV_ORDER; j++)
				printf(" %s", current->strings[j]);
			printf("\n");
			for (j = 0; j < current->num_exits; j++) {
				printf("  %d ->", current->exits[j].count);
				int k;
				for (k = 0; k < MARKOV_ORDER; k++)
					printf(" %s", current->exits[j].node->strings[k]);
				printf("\n");
			}
		}
	}
}

// Get some stats on the various hash tables
static void markov_stats(void)
{
	int max_depth;
	int total_depth;
	int num_filled;
	int count;
	int i;

	// String table
	max_depth = total_depth = num_filled = count = 0;
	for (i = 0; i < STRING_TABLE_SIZE; i++) {
		if (string_pool[i])
			num_filled++;

		int depth = 0;
		struct string_pool_t *current;
		for (current = string_pool[i]; current; current = current->next) {
			count++;
			depth++;
		}

		total_depth += depth * depth;
		max_depth = max(depth, max_depth);
	}
	printf("\nString table\n");
	printf("%d elements, %d/%d slots, load factor %f\n", count, num_filled, STRING_TABLE_SIZE, (float)count / STRING_TABLE_SIZE);
	printf("%d empty slots, %f usage \n", STRING_TABLE_SIZE - num_filled, (float)num_filled / STRING_TABLE_SIZE);
	printf("Max depth %d, average depth %f\n", max_depth, (float)total_depth / count);
	printf("Memory used by hash table structure: %zdk\n\n", STRING_TABLE_SIZE * sizeof(struct string_pool_t *) / 1024);

	// Start table
	max_depth = total_depth = num_filled = count = 0;
	for (i = 0; i < MARKOV_START_SIZE; i++) {
		if (markov_start_table[i])
			num_filled++;

		int depth = 0;
		struct markov_hash_exit_t *current;
		for (current = markov_start_table[i]; current; current = current->next) {
			count++;
			depth++;
		}

		total_depth += depth * depth;
		max_depth = max(depth, max_depth);
	}
	printf("Start table\n");
	printf("%d elements, %d/%d slots, load factor %f\n", count, num_filled, MARKOV_START_SIZE, (float)count / MARKOV_START_SIZE);
	printf("%d empty slots, %f usage \n", MARKOV_START_SIZE - num_filled, (float)num_filled / MARKOV_START_SIZE);
	printf("Max depth %d, average depth %f\n", max_depth, (float)total_depth / count);
	printf("Memory used by hash table structure: %zdk\n\n", MARKOV_START_SIZE * sizeof(struct markov_hash_exit_t *) / 1024);

	// Node table
	max_depth = total_depth = num_filled = count = 0;
	for (i = 0; i < MARKOV_TABLE_SIZE; i++) {
		if (markov_table[i])
			num_filled++;

		int depth = 0;
		struct markov_node_t *current;
		for (current = markov_table[i]; current; current = current->next) {
			count++;
			depth++;
		}

		total_depth += depth * depth;
		max_depth = max(depth, max_depth);
	}
	printf("Node table\n");
	printf("%d elements, %d/%d slots, load factor %f\n", count, num_filled, MARKOV_TABLE_SIZE, (float)count / MARKOV_TABLE_SIZE);
	printf("%d empty slots, %f usage \n", MARKOV_TABLE_SIZE - num_filled, (float)num_filled / MARKOV_TABLE_SIZE);
	printf("Max depth %d, average depth %f\n", max_depth, (float)total_depth / count);
	printf("Memory used by hash table structure: %zdk\n\n", MARKOV_TABLE_SIZE * sizeof(struct markov_node_t *) / 1024);

	// Print the number of allocated elements in each pool
	printf("Node pool: %d, %zdk mem usage\n", markov_nodepool.count, markov_nodepool.count * sizeof(struct markov_node_t) / 1024);
	printf("Hash exit pool: %d, %zdk mem usage\n", markov_hashexitpool.count, markov_hashexitpool.count * sizeof(struct markov_hash_exit_t) / 1024);
	for (i = 0; i < 16; i++)
		printf("%d exits pool: %d, %zdk mem usage\n", i + 1, markov_exitpool_small[i].count, markov_exitpool_small[i].count * (i + 1) * sizeof(struct markov_exit_t) / 1024);
	printf("32 exits pool: %d, %zdk mem usage\n", markov_exitpool_32.count, markov_exitpool_32.count * 32 * sizeof(struct markov_exit_t) / 1024);
	printf("64 exits pool: %d, %zdk mem usage\n", markov_exitpool_64.count, markov_exitpool_64.count * 64 * sizeof(struct markov_exit_t) / 1024);
	printf("128 exits pool: %d, %zdk mem usage\n", markov_exitpool_128.count, markov_exitpool_128.count * 128 * sizeof(struct markov_exit_t) / 1024);
	printf("Larger nodes: %d, %zdk mem usage\n", markov_largepool_count, markov_largepool_total * sizeof(struct markov_exit_t) / 1024);
	printf("String pool: %d strings, %dk mem usage\n", string_pool_count, string_mem_usage / 1024);
}

// First pass: Write the nodes to the file and leave holes for the exits
static inline void markov_export_nodes(FILE *file)
{
	int i;
	for (i = 0; i < MARKOV_TABLE_SIZE; i++) {
		struct markov_node_t *current;
		for (current = markov_table[i]; current; current = current->next) {
			// Create the node structure
			struct markov_export_node_t export;
			int j;
			for (j = 0; j < MARKOV_ORDER; j++)
				export.strings[j] = string_offset(current->strings[j]);
			export.num_exits = current->num_exits;

			// Save the node offset for the second pass
			current->offset = ftello64(file);

			// Write the node to the file
			if (!fwrite(&export, sizeof(struct markov_export_node_t), 1, file)) {
				printf("Error writing to markov database: %s\n", strerror(errno));
				exit(1);
			}

			// Leave a hole in the file for putting the exits
			fseeko64(file, sizeof(struct markov_export_exit_t) * current->num_exits, SEEK_CUR);
		}
	}
}

// Second pass: Write the exits in the holes from the first pass
static inline void markov_export_exits(FILE *file)
{
	int i;
	for (i = 0; i < MARKOV_TABLE_SIZE; i++) {
		struct markov_node_t *current;
		for (current = markov_table[i]; current; current = current->next) {
			// Go to the offset of the exits for this node
			fseeko64(file, current->offset + sizeof(struct markov_export_node_t), SEEK_SET);

			// Go through all the exits of this node
			int total_count = 0;
			if (current->num_exits > 128) {
				int j;
				int table_size = next_power_of_2(current->num_exits);
				for (j = 0; j < table_size; j++) {
					struct markov_hash_exit_t *current_exit;
					for (current_exit = current->hashtable[j]; current_exit; current_exit = current_exit->next) {
						total_count += current_exit->count;
						struct markov_export_exit_t export;
						export.node = current_exit->node->offset;
						export.count = total_count;
						if (!fwrite(&export, sizeof(struct markov_export_exit_t), 1, file)) {
							printf("Error writing to markov database: %s\n", strerror(errno));
							exit(1);
						}
					}
				}
			} else {
				int j;
				for (j = 0; j < current->num_exits; j++) {
					total_count += current->exits[j].count;
					struct markov_export_exit_t export;
					export.node = current->exits[j].node->offset;
					export.count = total_count;
					if (!fwrite(&export, sizeof(struct markov_export_exit_t), 1, file)) {
						printf("Error writing to markov database: %s\n", strerror(errno));
						exit(1);
					}
				}
			}
		}
	}
}

// Write all the start states
static inline void markov_export_start(FILE *file)
{
	// Write the number of start states
	if (!fwrite(&markov_num_start, sizeof(markov_num_start), 1, file)) {
		printf("Error writing to start database: %s\n", strerror(errno));
		exit(1);
	}

	int i;
	int total_count = 0;
	for (i = 0; i < MARKOV_START_SIZE; i++) {
		struct markov_hash_exit_t *current;
		for (current = markov_start_table[i]; current; current = current->next) {
			total_count += current->count;
			struct markov_export_exit_t export;
			export.node = current->node->offset;
			export.count = total_count;
			if (!fwrite(&export, sizeof(struct markov_export_exit_t), 1, file)) {
				printf("Error writing to start database: %s\n", strerror(errno));
				exit(1);
			}
		}
	}
}

// Export the markov model to a file
static inline void markov_export(void)
{
	// First create the string database
	FILE *file = fopen("stringdb", "w");
	if (!file) {
		printf("Error opening string database for writing: %s\n", strerror(errno));
		exit(1);
	}
	printf("Writing strings... ");
	fflush(stdout);
	string_export(file);
	if (fclose(file)) {
		printf("Error writing to string database: %s\n", strerror(errno));
		exit(1);
	}
	printf("done\n");

	// Then create the node and exit database
	file = fopen("markovdb", "w");
	if (!file) {
		printf("Error opening markov database for writing: %s\n", strerror(errno));
		exit(1);
	}
	printf("Writing markov nodes... ");
	fflush(stdout);
	markov_export_nodes(file);
	printf("done\n");
	printf("Writing markov exits... ");
	fflush(stdout);
	markov_export_exits(file);
	if (fclose(file)) {
		printf("Error writing to markov database: %s\n", strerror(errno));
		exit(1);
	}
	printf("done\n");

	// Finally create the start states database
	file = fopen("startdb", "w");
	if (!file) {
		printf("Error opening start database for writing: %s\n", strerror(errno));
		exit(1);
	}
	printf("Writing start states... ");
	fflush(stdout);
	markov_export_start(file);
	if (fclose(file)) {
		printf("Error writing to markov database: %s\n", strerror(errno));
		exit(1);
	}
	printf("done\n");
}

// Signal handler to allow interruption
static void signal_handler(int signal)
{
	// Shut up compiler warning
	(void)signal;
	exit(0);
}

// Main function, reads each line from the standard input as a word. Empty lines
// delimit a sentence.
int main(void)
{
	atexit(markov_stats);
	signal(SIGINT, signal_handler);
	markov_init();

	int counter = 0;
	int length = 0;
	const char *sentence[8192];
	char buffer[8192];
	while (fgets(buffer, sizeof(buffer), stdin)) {
		// General progress indicator, shows number of lines processed.
		counter++;
		if (counter % 100000 == 0)
			printf("%d\n", counter);

		// fgets returns a string with a newline at the end, except if we are
		// at the end of a file that doesn't have a trailing newline.
		if (buffer[strlen(buffer) - 1] == '\n')
			buffer[strlen(buffer) - 1] = '\0';
		else if (!feof(stdin))
			printf("Word too long\n");

		// Empty line means end of sentence
		if (!buffer[0]) {
			markov_train(length, sentence);
			length = 0;
		} else {
			sentence[length++] = string_copy(buffer);
			if (length == 8192) {
				printf("Sentence too long\n");
				markov_train(length, sentence);
				length = 0;
			}
		}
	}

	// Save the model
	markov_export();

	return 0;
}
