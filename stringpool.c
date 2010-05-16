#include <stdlib.h>
#include "stringpool.h"

// String pool table
struct string_pool_t *string_pool[STRING_POOL_TABLE_SIZE] = {NULL};

// Current memory block used for string allocation
void *string_mem = NULL;
int string_mem_offset = 0;
