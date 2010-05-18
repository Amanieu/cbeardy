#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include "hash.h"
#include "math.h"
#include "mempool.h"
#include "stringpool.h"

// Order of the markov model
#define MARKOV_ORDER 2

// Size of the markov chain node hash table
#define MARKOV_TABLE_SIZE 0x1000000

// Size of start node hash table
#define MARKOV_START_SIZE 0x200000

// Initial size of the string buffer when generating strings
#define MARKOV_GENERATE_BUFFER_SIZE 512

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

// An entry in the start node hash table
struct markov_start_t {
	struct markov_start_t *next;
	struct markov_node_t *node;
	int count;
};

// Hash table of markov chain nodes
static struct markov_node_t *markov_table[MARKOV_TABLE_SIZE];

// Hash table of start nodes
static struct markov_start_t *markov_start_table[MARKOV_START_SIZE];

// Memory pool for start node entries
static struct mempool_t markov_startpool;

// Memory pool for the node structure
static struct mempool_t markov_nodepool;

// Memory pools for exits with 1 to 16 elements
static struct mempool_t markov_exitpool_small[16];

// Memory pools for exits with 32, 64 and 128 elements
static struct mempool_t markov_exitpool_32;
static struct mempool_t markov_exitpool_64;
static struct mempool_t markov_exitpool_128;

#ifdef MEMORY_STATS
// Statistics for malloc()-based allocations
static int markov_largepool_count;
static int markov_largepool_total;
#endif

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
		if (node->num_exits == 0)
			newexits = mempool_alloc(&markov_exitpool_small[0], sizeof(struct markov_exit_t));
		else if (node->num_exits < 16) {
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
#ifdef MEMORY_STATS
			markov_largepool_count++;
			markov_largepool_total += 256;
#endif
		} else {
			newexits = realloc(node->exits, sizeof(struct markov_exit_t) * node->num_exits * 2);
#ifdef MEMORY_STATS
			markov_largepool_total += node->num_exits + node->num_exits / 2;
#endif
		}
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
	int hash = hash_pointer(node) & (MARKOV_START_SIZE - 1);

	// Search the hash table for the node
	struct markov_start_t *start;
	for (start = markov_start_table[hash]; start; start = start->next) {
		if (start->node == node) {
			start->count++;
			return;
		}
	}

	// Allocate a new entry and add it to the hash table
	start = mempool_alloc(&markov_startpool, sizeof(struct markov_start_t));
	start->count = 1;
	start->node = node;
	start->next = markov_start_table[hash];
	markov_start_table[hash] = start;
}

// Train the markov model using the given sentence. All strings in the sentence
// must have been allocated using copy_string().
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

// Picks a random start state, taking into account weightings based on frequency
static struct markov_node_t *markov_generate_start_state(void)
{
	int i;
	struct markov_start_t *current;
	
	int frequency_sum = 0;
	
	// Calculate the sum of the frequencies of the start table
	for (i = 0; i < MARKOV_START_SIZE; i++) {
		for (current = markov_start_table[i]; current; current = current->next) {
			frequency_sum += current->count;
		}
	}
	
	// TODO: This is probably bad for a good number of reasons.
	//       What should I be doing?
	int frequency_threshold = rand()%frequency_sum;
	
	// Iterate through the data set until reaching the random threshold
	frequency_sum = 0;
	for (i = 0; i < MARKOV_START_SIZE; i++) {
		for (current = markov_start_table[i]; current; current = current->next) {
			frequency_sum += current->count;
			
			if (frequency_sum > frequency_threshold)
				return current->node;
		}
	}
	
	// Shouldn't reach here as threshold should have been reached
	assert(0);
	return NULL;
}

// Picks a random exit state, taking into account weightings based on frequency.
// Returns NULL if the state should be the END state.
static struct markov_node_t *markov_generate_next_state(struct markov_node_t *current_node)
{
	if (current_node->strings[MARKOV_ORDER-1] == NULL)
		return NULL;
	
	int i;
	int frequency_sum = 0;
	struct markov_exit_t *current_exit;
	
	// Calculate the sum of the frequencies of the start table
	current_exit = current_node->exits;
	for (i = 0; i < current_node->num_exits; i++, current_exit++) {
		frequency_sum += current_exit++->count;
	}
	
	// TODO: This is probably bad for a good number of reasons.
	//       What should I be doing?
	int frequency_threshold = rand()%frequency_sum;
	
	// Iterate through the data set until reaching the random threshold
	current_exit = current_node->exits;
	for (i = 0; i < current_node->num_exits; i++, current_exit++) {
		frequency_sum += current_exit->count;
		
		if (frequency_sum > frequency_threshold)
			return current_exit->node;
	}
	
	// There were no exits!
	return NULL;
}

// Appends a node's contents to a given buffer and returns a pointer to the
// buffer incase it is extended.
static char *markov_append_node_to_string(char *old_str,
                                          int *buffer_size,
                                          struct markov_node_t *node,
                                          int only_print_last)
{
	// The index to start printing the strings in the node from
	int print_from = only_print_last * (MARKOV_ORDER - 1);
	
	// Find the new required length for the string incase the buffer needs
	// extending
	size_t new_length;
	new_length = strlen(old_str);
	int i;
	for (i = print_from; i < MARKOV_ORDER; i++) {
		if (node->strings[i])
			new_length += strlen(node->strings[i]) + 1;
	}
	
	char *new_str;
	
	if ((int)new_length + 1 > *buffer_size) {
		// If the new length is too big, double the buffer size
		(*buffer_size) *= 2;
		new_str = realloc(old_str, *buffer_size);
	} else {
		new_str = old_str;
	}
	
	// Concatenate the new node's strings onto the end
	for (i = print_from; i < MARKOV_ORDER; i++) {
		if (node->strings[i]) {
			strcat(new_str, node->strings[i]);
			strcat(new_str, " ");
		}
	}
	
	return new_str;
}

// Generate sentences using the current markov model
static char *markov_generate()
{
	// Create a buffer to put the output into
	int buffer_size = MARKOV_GENERATE_BUFFER_SIZE;
	char *output = malloc(MARKOV_GENERATE_BUFFER_SIZE);
	*output = '\0';
	
	struct markov_node_t *start = markov_generate_start_state();
	output = markov_append_node_to_string(output, &buffer_size, start, 0);
	
	int count = 0;
	
	struct markov_node_t *current_node = start;
	while (current_node) {
		count++;
		if (count > 50) {
			printf("!!!!!!!!!!!!!!!!!!!! Looping !!!!!!!!!!!!!!!!!!!!\n");
			break;
		}
		
		current_node = markov_generate_next_state(current_node);
		if (current_node)
			output = markov_append_node_to_string(output, &buffer_size, current_node, 1);
	}
	
	return output;
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
		struct markov_start_t *current;
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
		if (depth > max_depth)
			max_depth = depth;
	}
	printf("\nString table\n");
	printf("%d elements, %d/%d slots, load factor %f\n", count, num_filled, STRING_TABLE_SIZE, (float)count / STRING_TABLE_SIZE);
	printf("%d empty slots, %f usage \n", STRING_TABLE_SIZE - num_filled, (float)num_filled / STRING_TABLE_SIZE);
	printf("Max depth %d, average depth %f\n", max_depth, (float)total_depth / count);
	printf("Memory used by hash table structure: %zd\n\n", STRING_TABLE_SIZE * sizeof(struct string_pool_t *));

	// Start table
	max_depth = total_depth = num_filled = count = 0;
	for (i = 0; i < MARKOV_START_SIZE; i++) {
		if (markov_start_table[i])
			num_filled++;

		int depth = 0;
		struct markov_start_t *current;
		for (current = markov_start_table[i]; current; current = current->next) {
			count++;
			depth++;
		}

		total_depth += depth * depth;
		if (depth > max_depth)
			max_depth = depth;
	}
	printf("Start table\n");
	printf("%d elements, %d/%d slots, load factor %f\n", count, num_filled, MARKOV_START_SIZE, (float)count / MARKOV_START_SIZE);
	printf("%d empty slots, %f usage \n", MARKOV_START_SIZE - num_filled, (float)num_filled / MARKOV_START_SIZE);
	printf("Max depth %d, average depth %f\n", max_depth, (float)total_depth / count);
	printf("Memory used by hash table structure: %zd\n\n", MARKOV_START_SIZE * sizeof(struct markov_start_t *));

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
		if (depth > max_depth)
			max_depth = depth;
	}
	printf("Node table\n");
	printf("%d elements, %d/%d slots, load factor %f\n", count, num_filled, MARKOV_TABLE_SIZE, (float)count / MARKOV_TABLE_SIZE);
	printf("%d empty slots, %f usage \n", MARKOV_TABLE_SIZE - num_filled, (float)num_filled / MARKOV_TABLE_SIZE);
	printf("Max depth %d, average depth %f\n", max_depth, (float)total_depth / count);
	printf("Memory used by hash table structure: %zd\n\n", MARKOV_TABLE_SIZE * sizeof(struct markov_node_t *));

#ifdef MEMORY_STATS
	// Print the number of allocated elements in each pool
	printf("Node pool: %d, %zd mem usage\n", markov_nodepool.count, markov_nodepool.count * sizeof(struct markov_node_t));
	printf("Start pool: %d, %zd mem usage\n", markov_startpool.count, markov_startpool.count * sizeof(struct markov_start_t));
	for (i = 0; i < 16; i++)
		printf("%d exits pool: %d, %zd mem usage\n", i + 1, markov_exitpool_small[i].count, markov_exitpool_small[i].count * (i + 1) * sizeof(struct markov_exit_t));
	printf("32 exits pool: %d, %zd mem usage\n", markov_exitpool_32.count, markov_exitpool_32.count * 32 * sizeof(struct markov_exit_t));
	printf("64 exits pool: %d, %zd mem usage\n", markov_exitpool_64.count, markov_exitpool_64.count * 64 * sizeof(struct markov_exit_t));
	printf("128 exits pool: %d, %zd mem usage\n", markov_exitpool_128.count, markov_exitpool_128.count * 128 * sizeof(struct markov_exit_t));
	printf("Larger nodes: %d, %zd mem usage\n", markov_largepool_count, markov_largepool_total * sizeof(struct markov_exit_t));
	printf("String pool: %d strings, %d mem usage\n", string_pool_count, string_mem_usage);
#endif
}

// Signal handler to allow interruption
void signal_handler(int signal)
{
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
		// General progress indicator, compare it with input line count
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
			sentence[length++] = copy_string(buffer);
			if (length == 8192) {
				printf("Sentence too long\n");
				markov_train(length, sentence);
				length = 0;
			}
		}
	}
	
	printf("\n---\n");
	int i;
	for (i = 0; i < 20; i++) {
		char *sentence = markov_generate();
		printf("%s\n\n", sentence);
		free(sentence);
	}
	printf("---\n");
	
	return 0;
}
