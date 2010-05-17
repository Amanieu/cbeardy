#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "hash.h"
#include "mempool.h"
#include "stringpool.h"

// Order of the markov model
#define MARKOV_ORDER 2

// Size of the markov chain node hash table
#define MARKOV_TABLE_SIZE 0x100000

// Initial allocation for start nodes list
#define MARKOV_START_NODES_SIZE 0x80000

// An exit for a node in a markov chain
struct markov_node_t;
struct markov_exit_t {
	struct markov_node_t *node;
	int count;
};

// A node in a markov chain
struct markov_node_t {
	struct markov_node_t *next;
	const char *strings[MARKOV_ORDER];
	int num_exits;
	struct markov_exit_t *exits;
};

// Hash table of markov chain nodes
static struct markov_node_t *markov_table[MARKOV_TABLE_SIZE];

// List of start nodes
static struct markov_exit_t *markov_start_nodes = NULL;
static int markov_num_start_nodes = 0;

// Memory pool for the node structure
static struct mempool_t markov_nodepool;

// Memory pools for exits with 1 to 16 elements
static struct mempool_t markov_exitpool_small[16];

// Memory pools for exits with 32, 64 and 128 elements
static struct mempool_t markov_exitpool_32;
static struct mempool_t markov_exitpool_64;
static struct mempool_t markov_exitpool_128;

// Search the hash table for a node. Allocates a new node if one wasn't found.
// All strings should have been allocated using copy_string().
static inline struct markov_node_t *markov_get_node(const char *const *strings)
{
	int hash = hash_strings(MARKOV_ORDER, strings) & (MARKOV_TABLE_SIZE - 1);

	// Search the hash table for the node
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

// Add an exit to a node
static inline void markov_add_exit(struct markov_node_t *node, struct markov_node_t *exit)
{
	// First see if we already have this exit
	int i;
	for (i = 0; i < node->num_exits; i++) {
		if (node->exits[i].node == exit) {
			node->exits[i].count++;
			return;
		}
	}

	// We need to add a new exit. See if we need to extend the exit list.
	if (node->num_exits <= 16 || is_power_of_2(node->num_exits)) {
		struct markov_exit_t *newexits;
		if (node->num_exits == 0) {
			newexits = mempool_alloc(&markov_exitpool_small[0], sizeof(struct markov_exit_t));
		} else if (node->num_exits < 16) {
			struct mempool_t *oldpool = &markov_exitpool_small[node->num_exits - 1];
			struct mempool_t *newpool = &markov_exitpool_small[node->num_exits];
			newexits = mempool_alloc(newpool, sizeof(struct markov_exit_t) * (node->num_exits + 1));
			memcpy(newexits, node->exits, sizeof(struct markov_exit_t) * node->num_exits);
			mempool_free(oldpool, node->exits);
		} else if (node->num_exits == 16) {
			newexits = mempool_alloc(&markov_exitpool_32, sizeof(struct markov_exit_t) * 32);
			memcpy(newexits, node->exits, sizeof(struct markov_exit_t) * 16);
			mempool_free(&markov_exitpool_small[15], node->exits);
		} else if (node->num_exits == 32) {
			newexits = mempool_alloc(&markov_exitpool_64, sizeof(struct markov_exit_t) * 64);
			memcpy(newexits, node->exits, sizeof(struct markov_exit_t) * 32);
			mempool_free(&markov_exitpool_32, node->exits);
		} else if (node->num_exits == 64) {
			newexits = mempool_alloc(&markov_exitpool_128, sizeof(struct markov_exit_t) * 128);
			memcpy(newexits, node->exits, sizeof(struct markov_exit_t) * 64);
			mempool_free(&markov_exitpool_64, node->exits);
		} else if (node->num_exits == 128) {
			newexits = malloc(sizeof(struct markov_exit_t) * 256);
			memcpy(newexits, node->exits, sizeof(struct markov_exit_t) * 128);
			mempool_free(&markov_exitpool_128, node->exits);
		} else
			newexits = realloc(node->exits, sizeof(struct markov_exit_t) * node->num_exits * 2);
		node->exits = newexits;
	}

	// Now just add an exit at the end
	int exit_id = node->num_exits++;
	node->exits[exit_id].node = exit;
	node->exits[exit_id].count = 1;
}

// Add a node to the start of the chain
static inline void markov_add_start(struct markov_node_t *node)
{
	// Look for an existing entry in the start list for this node
	int i;
	for (i = 0; i < markov_num_start_nodes; i++) {
		if (markov_start_nodes[i].node == node) {
			markov_start_nodes[i].count++;
			return;
		}
	}

	// Extend list if needed
	if (markov_num_start_nodes >= MARKOV_START_NODES_SIZE && is_power_of_2(markov_num_start_nodes))
		markov_start_nodes = realloc(markov_start_nodes, markov_num_start_nodes * 2);

	// Add new entry to start list
	int entry_id = markov_num_start_nodes++;
	markov_start_nodes[entry_id].node = node;
	markov_start_nodes[entry_id].count = 1;
}

// Train the markov model using the given sentence. All strings in the sentence
// must have been allocated using copy_string().
void markov_train(int length, const char *const *sentence)
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
void markov_init(void)
{
	string_init();
	markov_start_nodes = calloc(1, sizeof(struct markov_exit_t) * MARKOV_START_NODES_SIZE);
}

// Print the entire model
void markov_print(void)
{
	printf("START\n");
	int i;
	for (i = 0; i < markov_num_start_nodes; i++) {
		printf("  %d ->", markov_start_nodes[i].count);
		int j;
		for (j = 0; j < MARKOV_ORDER; j++)
			printf(" %s", markov_start_nodes[i].node->strings[j]);
		printf("\n");
	}

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

// Main test function
int main(void)
{
	markov_init();
	const char *sentence[256];
	int length = 0;
	char buffer[1024];
	while (fgets(buffer, sizeof(buffer), stdin)) {
		if (buffer[0] == '\n') {
			markov_train(length, sentence);
			markov_print();
			length = 0;
		} else {
			buffer[strlen(buffer) - 1] = '\0';
			sentence[length++] = copy_string(buffer);
		}
	}

	return 0;
}
