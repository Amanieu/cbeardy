#ifndef STRINGPOOL_H_
#define STRINGPOOL_H

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include "hash.h"
#include "markov.h"
#include "math.h"

// Size of the string pool hash table
#define STRING_TABLE_SIZE 0x400000

// Size of a block of memory for use in the string pool
#define STRING_BLOCK_SIZE 0x400000

// String pool hash table entry
struct string_pool_t {
	struct string_pool_t *next;
	union {
		char string[0];
		string_offset_t offset;
	};
};

// String pool table
extern struct string_pool_t *string_pool[STRING_TABLE_SIZE];

// Current memory block used for string allocation
extern void *string_mem;
extern int string_mem_offset;

// Amount of memory used by string pool
extern int string_mem_usage;
extern int string_pool_count;

// Allocate a copy of a string, or return an existing copy
static inline const char *string_copy(const char *string)
{
	// Hash the string
	int hash = hash_string(string) & (STRING_TABLE_SIZE - 1);

	// Search the table for the string
	struct string_pool_t *current;
	for (current = string_pool[hash]; current; current = current->next) {
		if (!strcmp(current->string, string))
			return current->string;
	}

	// String was not found, so allocate a copy and add it to the hash table
	unsigned int length = sizeof(struct string_pool_t *) + strlen(string) + 1;

	// Make sure we have enough space for the offset
	length = max(length, sizeof(struct string_pool_t));

	// Make sure length is properly aligned to machine word length
	length = align(length, sizeof(void *));

	// Track memory usage
	string_mem_usage += length;
	string_pool_count++;

	// Try to allocate from current memory block, get a new block if full
	if (string_mem_offset + length > STRING_BLOCK_SIZE) {
		string_mem = malloc(STRING_BLOCK_SIZE);
		string_mem_offset = length;
		current = string_mem;
	} else {
		current = string_mem + string_mem_offset;
		string_mem_offset += length;
	}

	// Add string to hash table and return it
	current->next = string_pool[hash];
	strcpy(current->string, string);
	string_pool[hash] = current;
	return current->string;
}

// Initialize string pool
static inline void string_init(void)
{
	string_mem = malloc(STRING_BLOCK_SIZE);
}

// Get the offset of a string in the string file
static inline string_offset_t string_offset(const char *string)
{
	if (!string)
		return -1;
	struct string_pool_t *current = (void *)string - offsetof(struct string_pool_t, string);
	return current->offset;
}

// Write the string pool to a file. Note that string won't be readable anymore
// after this operation.
static inline void string_export(FILE *file)
{
	string_offset_t offset = 0;
	int i;
	for (i = 0; i < STRING_TABLE_SIZE; i++) {
		struct string_pool_t *current;
		for (current = string_pool[i]; current; current = current->next) {
			int length = strlen(current->string) + 1;
			if (!fwrite(current->string, length, 1, file)) {
				printf("Error writing to string database: %s\n", strerror(errno));
				exit(1);
			}
			current->offset = offset;
			offset += length;
		}
	}
}

#endif
