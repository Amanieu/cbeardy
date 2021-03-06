#ifndef MEMPOOL_H_
#define MEMPOOL_H_

#include <stdlib.h>

// Size of a block for mempool allocation
#define MEMPOOL_BLOCK_SIZE 262144

// A memory pool
struct mempool_t {
	struct mempool_t *next;
	int count;
};

// Slow path: allocate a large memory block and split it
static inline void *mempool_alloc_slow(struct mempool_t *pool, int size)
{
	int alloc_size = MEMPOOL_BLOCK_SIZE - (MEMPOOL_BLOCK_SIZE % size);
	void *block = malloc(alloc_size);
	assert(block);
	void *pos;
	for (pos = block; pos < block + alloc_size - size; pos += size) {
		struct mempool_t *current = pos;
		current->next = pool->next;
		pool->next = current;
	}

	// Return the last element (which wasn't added to the pool)
	return pos;
}

// Allocate an item from a memory pool
static inline void *mempool_alloc(struct mempool_t *pool, int size)
{
	// Fast path if there are items in the pool
	pool->count++;
	if (pool->next) {
		struct mempool_t *current = pool->next;
		pool->next = current->next;
		return current;
	}

	return mempool_alloc_slow(pool, size);
}

// Release an item back to a memory pool
static inline void mempool_free(struct mempool_t *pool, void *ptr)
{
	// Just add the item back into the pool's free list
	pool->count--;
	struct mempool_t *current = ptr;
	current->next = pool->next;
	pool->next = current;
}

#endif
