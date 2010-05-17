#include <stdlib.h>
#include <string.h>
#include "hash.h"
#include "mempool.h"
#include "stringpool.h"

// Order of the markov model
#define MARKOV_ORDER 2

// Size of the markov chain node hash table
#define MARKOV_TABLE_SIZE 0x100000

// Initial allocation for start nodes list
#define MARKOV_START_NODES_SIZE 1048576

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

// List of start nodes
static struct markov_exit_t *markov_start_nodes = NULL;
static int markov_num_start_nodes = 0;

// Memory pools for nodes with 0 to 16 elements
static struct mempool_t markov_nodepool_small[17];

// Memory pools for nodes with 32, 64 and 128 elements.
static struct mempool_t markov_nodepool_32;
static struct mempool_t markov_nodepool_64;
static struct mempool_t markov_nodepool_128;

// Search the hash table for a node. Allocates a new node if one wasn't found.
// All strings should have been allocated using copy_string(). Also returns a
// pointer to the insertion point of this node. This is the pointer that must be
// modified if the node is moved.
static inline struct markov_node_t *markov_get_node(const char *const *strings, struct markov_node_t ***insert)
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

	// Allocate a new node
	current = mempool_alloc(&markov_nodepool_small[0], sizeof(struct markov_node_t));
	current->next = NULL;
	current->num_exits = 0;
	int i;
	for (i = 0; i < MARKOV_ORDER; i++)
		current->strings[i] = strings[i];
	**insert = current;
	return current;
}

// Add an exit to a node. You must provide the insertion point of the node.
static inline void markov_add_exit(struct markov_node_t *node, struct markov_node_t **insert, struct markov_node_t *exit)
{
	// First see if the node already has this exit
	int i;
	for (i = 0; i < node->num_exits; i++) {
		if (node->exits[i].node == exit) {
			node->exits[i].count++;
			return;
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
}

// Add a node to the start of the chain. Returns the built node and its
// insertion point.
static inline struct markov_node_t *markov_add_start(const char *const *strings, struct markov_node_t ***insert)
{
	// Get a node for these strings
	struct markov_node_t *node = markov_get_node(strings, insert);

	// Look for an existing entry in the start list for this node
	int i;
	for (i = 0; i < markov_num_start_nodes; i++) {
		if (markov_start_nodes[i].node == node) {
			markov_start_nodes[i].count++;
			return node;
		}
	}

	// Add new entry to start list
	if (markov_num_start_nodes >= MARKOV_START_NODES_SIZE && (markov_num_start_nodes & (markov_num_start_nodes - 1)) == 0)
		markov_start_nodes = realloc(markov_start_nodes, markov_num_start_nodes * 2);
	int entry_id = markov_num_start_nodes++;
	markov_start_nodes[entry_id].node = node;
	markov_start_nodes[entry_id].count = 1;
	return node;
}

// Train the markov model using the given sentence. All strings in the sentence
// must have been allocated using copy_string().
void markov_train(int length, const char *const *sentence)
{
	// Ignore empty sentences
	if (!length)
		return;

	// Catch small sentences
	if (length < MARKOV_ORDER) {
		const char *buffer[MARKOV_ORDER];
		int i;
		for (i = 0; i < length; i++)
			buffer[i] = sentence[i];
		for (i = length; i < MARKOV_ORDER; i++)
			buffer[i] = NULL;
		struct markov_node_t **insert;
		markov_add_start(buffer, &insert);
		return;
	}

	// Build first node
	struct markov_node_t **insert;
	struct markov_node_t *node = markov_add_start(sentence, &insert);

	// Build middle nodes
	int i;
	for (i = MARKOV_ORDER; i < length; i++) {
		sentence++;
		struct markov_node_t **nextinsert;
		struct markov_node_t *nextnode = markov_get_node(sentence, &nextinsert);
		markov_add_exit(node, insert, nextnode);
		insert = nextinsert;
		node = nextnode;
	}

	// Build end node
	const char *buffer[MARKOV_ORDER];
	for (i = 0; i < MARKOV_ORDER - 1; i++)
		buffer[i] = sentence[i];
	buffer[MARKOV_ORDER - 1] = NULL;
	struct markov_node_t **nextinsert;
	struct markov_node_t *nextnode = markov_get_node(buffer, &nextinsert);
	markov_add_exit(node, insert, nextnode);
}

// Initialize various stuff
void markov_init(void)
{
	markov_start_nodes = malloc(sizeof(struct markov_exit_t) * MARKOV_START_NODES_SIZE);
}
