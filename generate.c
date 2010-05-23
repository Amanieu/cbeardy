#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "markov.h"

// Initial size of the string buffer when generating strings
#define MARKOV_GENERATE_BUFFER_SIZE 512

// Memory-mapped database files
static char *stringdb;
static void *markovdb;
static markov_offset_t markovdb_length;
static struct markov_export_start_t *startdb;

// Memory map a file
static inline void *mmap_file(const char *file, int64_t *length_ptr)
{
	// Open the file
	int fd = open(file, O_RDONLY);
	if (fd == -1) {
		printf("Error opening file %s: %s\n", file, strerror(errno));
		exit(1);
	}

	// Get the file length
	struct stat buf;
	fstat(fd, &buf);
	markov_offset_t length = buf.st_size;

	// Make sure length fits in our address space
	if (sizeof(void *) == 4 && length > 0xFFFFFFFF)
		printf("Warning: File too big for 32bit address space\n");

	if (length_ptr)
		*length_ptr = length;

	// Memory map the file
	void *ptr = mmap(NULL, length, PROT_READ, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED) {
		printf("Error mmaping file %s: %s\n", file, strerror(errno));
		exit(1);
	}

	return ptr;
}

// Get a string from its offset
static inline const char *get_string(string_offset_t offset)
{
	if (offset == -1)
		return NULL;
	else
		return stringdb + offset;
}

// Get a node from its offset
static inline struct markov_export_node_t *get_node(markov_offset_t offset)
{
	return (struct markov_export_node_t *)(markovdb + offset);
}

// Picks a random exit state, taking into account weightings based on frequency.
static inline struct markov_export_node_t *markov_generate_next_state(int num_exits, struct markov_export_exit_t *exits)
{
	// Determine the frequencry threshold
	int frequency_threshold = rand() % (exits[num_exits - 1].count + 1);

	// Use a binary search to find the exit we are looking for
	int half;
	struct markov_export_exit_t *middle;
	while (num_exits) {
		half = num_exits / 2;
		middle = exits + half;
		if (middle->count < frequency_threshold) {
			exits = middle + 1;
			num_exits = num_exits - half - 1;
		} else
			num_exits = half;
	}

	return get_node(exits->node);
}

// Appends a node's contents to a given buffer and returns a pointer to the
// buffer incase it is extended.
static inline char *markov_append_node_to_string(char *old_str,
                                                 int *buffer_size,
                                                 struct markov_export_node_t *node,
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
		if (node->strings[i] != -1)
			new_length += strlen(get_string(node->strings[i])) + 1;
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
		if (node->strings[i] != -1) {
			strcat(new_str, get_string(node->strings[i]));
			strcat(new_str, " ");
		}
	}

	return new_str;
}

// Generate sentences using the current markov model
static inline char *markov_generate()
{
	// Create a buffer to put the output into
	int buffer_size = MARKOV_GENERATE_BUFFER_SIZE;
	char *output = malloc(MARKOV_GENERATE_BUFFER_SIZE);
	*output = '\0';

	struct markov_export_node_t *start = markov_generate_next_state(startdb->num_start_states, startdb->start_states);
	output = markov_append_node_to_string(output, &buffer_size, start, 0);

	struct markov_export_node_t *current_node = start;
	while (current_node->strings[MARKOV_ORDER-1] != -1) {
		current_node = markov_generate_next_state(current_node->num_exits, current_node->exits);
		output = markov_append_node_to_string(output, &buffer_size, current_node, 1);
	}

	return output;
}

// Main function
int main(void)
{
	// Read the databases
	stringdb = mmap_file("stringdb", NULL);
	markovdb = mmap_file("markovdb", &markovdb_length);
	startdb = mmap_file("startdb", NULL);

	// Generate strings until interrupted by a signal
	while (true) {
		char *string = markov_generate();
		printf("%s\n\n", string);
		free(string);
		while (getchar() != '\n') {}
	}

	return 0;
}
