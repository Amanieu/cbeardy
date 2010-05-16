#ifndef MEMPOOL_H_
#define MEMPOOL_H_

// Memory pool system

#include <stdlib.h>

// Size of a block for mempool allocation
#define MEMPOOL_BLOCK_SIZE 65536

// A memory pool
struct mempool_t {
	struct mempool_t *next;
};

// Allocate an item from a memory pool
static inline void *mempool_alloc(struct mempool_t *pool, size_t size)
{
	// Fast path if there are items in the pool
	if (pool->next) {
		struct mempool_t *current = pool->next;
		pool->next = current->next;
		return current;
	}

	// Slow path: allocate a large memory block and split it
	void *block = malloc(MEMPOOL_BLOCK_SIZE - (MEMPOOL_BLOCK_SIZE % size));
	void *pos;
	for (pos = block; pos < block + MEMPOOL_BLOCK_SIZE - size; pos += size) {
		struct mempool_t *current = pos;
		current->next = pool->next;
		pool->next = current;
	}

	// Return the last element (which wasn't added to the pool)
	return pos;
}

// Release an item back to a memory pool
static inline void mempool_free(struct mempool_t *pool, void *ptr)
{
	// Just add the item back into the pool's free list
	struct mempool_t *current = ptr;
	current->next = pool->next;
	pool->next = current;
}

#endif
