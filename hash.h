#ifndef HASH_H_
#define HASH_H_

#include <stdint.h>

// djb2 hash function, from http://www.cse.yorku.ca/~oz/hash.html
static inline int hash_string(const char *string)
{
	unsigned int hash = 5381;
	unsigned int c;

	while ((c = *string++))
		hash = (hash << 5) + hash + c;

	return hash;
}

// Hash a pointer (from boost::hash)
static inline int hash_pointer(const void *ptr)
{
	intptr_t value = (intptr_t)ptr;
	return value + (value >> 3);
}

// Hashes multiple strings. Since all strings are from the pool, just hash their
// pointers. (from boost::hash)
static inline int hash_strings(int num_strings, const char *const *strings)
{
	int hash = 0;
	int i;

	for (i = 0; i < num_strings; i++)
		hash ^= hash_pointer(strings[i]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);

	return hash;
}

#endif
