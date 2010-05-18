#include <stdlib.h>
#include "stringpool.h"

// String pool table
struct string_pool_t *string_pool[STRING_TABLE_SIZE];

// Current memory block used for string allocation
void *string_mem;
int string_mem_offset;

#ifdef MEMORY_STATS
// Amount of memory used by string pool
int string_mem_usage;
int string_pool_count;
#endif
