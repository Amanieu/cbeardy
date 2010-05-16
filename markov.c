#include <stdlib.h>
#include <string.h>
#include "hash.h"
#include "mempool.h"
#include "stringpool.h"

// Order of the markov model
#define MARKOV_ORDER 2

// Size of the markov chain node hash table
#define MARKOV_TABLE_SIZE 0x100000

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
	struct markov_exit_t exits[0];
};

// Hash table of markov chain nodes
static struct markov_node_t *markov_table[STRING_POOL_TABLE_SIZE] = {NULL};

// Memory pools for nodes with 0 to 16 elements
static struct mempool_t markov_nodepool_small[17];

// Memory pools for nodes with 32, 64 and 128 elements.
static struct mempool_t markov_nodepool_32;
static struct mempool_t markov_nodepool_64;
static struct mempool_t markov_nodepool_128;

// Search the hash table for a node. Returns NULL if the node is not found.
// All strings should have been allocated using copy_string(). Also returns a
// pointer to the insertion point of this node. This is the pointer that must be
// modified if the node is moved.
static inline struct markov_node_t *markov_find_node(const char **strings, struct markov_node_t ***insert)
{
	int hash = hash_strings(MARKOV_ORDER, strings);

	if (insert)
		*insert = &markov_table[hash];

	struct markov_node_t *current;
	for (current = markov_table[hash]; current; current = current->next) {
		int i;
		for (i = 0; i < MARKOV_ORDER; i++) {
			if (current->strings[i] != strings[i])
				break;
		}

		if (i == MARKOV_ORDER)
			return current;
		else if (insert)
			*insert = &current->next;
	}

	return NULL;
}

// Add an exit to a node. You must provide the insertion point of the node.
// Returns a pointer to the updated node.
static inline struct markov_node_t *markov_add_exit(struct markov_node_t *node, struct markov_node_t **insert, struct markov_node_t *exit)
{
	// First see if the node already has this exit
	int i;
	for (i = 0; i < node->num_exits; i++) {
		if (node->exits[i].node == exit) {
			node->exits[i].count++;
			return node;
		}
	}

	// We need to add a new exit. See if we need to extend the node structure.
	if (node->num_exits <= 16 || (node->num_exits & (node->num_exits - 1)) == 0) {
		struct markov_node_t *newnode;
		if (node->num_exits < 16) {
			struct mempool_t *oldpool = &markov_nodepool_small[node->num_exits];
			struct mempool_t *newpool = &markov_nodepool_small[node->num_exits + 1];
			newnode = mempool_alloc(newpool, sizeof(struct markov_node_t) + sizeof(struct markov_exit_t) * (node->num_exits + 1));
			memcpy(newnode, node, sizeof(struct markov_node_t) + sizeof(struct markov_exit_t) * node->num_exits);
			mempool_free(oldpool, node);
		} else if (node->num_exits == 16) {
			newnode = mempool_alloc(&markov_nodepool_32, sizeof(struct markov_node_t) + sizeof(struct markov_exit_t) * 32);
			memcpy(newnode, node, sizeof(struct markov_node_t) + sizeof(struct markov_exit_t) * 16);
			mempool_free(&markov_nodepool_small[16], node);
		} else if (node->num_exits == 32) {
			newnode = mempool_alloc(&markov_nodepool_64, sizeof(struct markov_node_t) + sizeof(struct markov_exit_t) * 64);
			memcpy(newnode, node, sizeof(struct markov_node_t) + sizeof(struct markov_exit_t) * 32);
			mempool_free(&markov_nodepool_32, node);
		} else if (node->num_exits == 64) {
			newnode = mempool_alloc(&markov_nodepool_128, sizeof(struct markov_node_t) + sizeof(struct markov_exit_t) * 128);
			memcpy(newnode, node, sizeof(struct markov_node_t) + sizeof(struct markov_exit_t) * 64);
			mempool_free(&markov_nodepool_64, node);
		} else if (node->num_exits == 128) {
			newnode = malloc(sizeof(struct markov_node_t) + sizeof(struct markov_exit_t) * 256);
			memcpy(newnode, node, sizeof(struct markov_node_t) + sizeof(struct markov_exit_t) * 128);
			mempool_free(&markov_nodepool_128, node);
		} else
			newnode = realloc(node, sizeof(struct markov_node_t) + sizeof(struct markov_exit_t) * node->num_exits * 2);
		*insert = newnode;
		node = newnode;
	}

	// Now just add an exit at the end
	int exit_id = node->num_exits++;
	node->exits[exit_id].node = exit;
	node->exits[exit_id].count = 1;

	return node;
}

// Train the markov model using the given sentence
void markov_train(int sentence_length, const char *const *sentence)
{
}
