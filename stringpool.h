#ifndef STRINGPOOL_H_
#define STRINGPOOL_H_

// String pool system

#include <stdlib.h>
#include <string.h>
#include "hash.h"

// Size of the string pool hash table
#define STRING_POOL_TABLE_SIZE 0x80000

// Size of a block of memory for use in the string pool
#define STRING_POOL_BLOCK_SIZE 4194304

// String pool hash table entry
struct string_pool_t {
	struct string_pool_t *next;
	char string[0];
};

// String pool table
extern struct string_pool_t *string_pool[STRING_POOL_TABLE_SIZE];

// Current memory block used for string allocation
extern void *string_mem;
extern int string_mem_offset;

// Allocate a copy of a string, or return an existing copy
static inline const char *copy_string(const char *string)
{
	// Hash the string
	int hash = hash_string(string) & (STRING_POOL_TABLE_SIZE - 1);

	// Search the table for the string
	struct string_pool_t *current;
	for (current = string_pool[hash]; current; current = current->next) {
		if (!strcmp(current->string, string))
			return current->string;
	}

	// String was not found, so allocate a copy and add it to the hash table
	int length = sizeof(struct string_pool_t) + strlen(string) + 1;

	// Make sure length is properly aligned to machine word length
	length = (length + sizeof(void *) - 1) & ~(sizeof(void *) - 1);

	// Try to allocate from current memory block, get a new block if full
	if (string_mem_offset + length > STRING_POOL_BLOCK_SIZE) {
		string_mem = malloc(STRING_POOL_BLOCK_SIZE);
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
	string_mem = malloc(STRING_POOL_BLOCK_SIZE);
}

#endif
