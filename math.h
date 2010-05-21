#ifndef MATH_H_
#define MATH_H_

#include <stdbool.h>

// Various math functions

// Check if a value is a power of 2
static inline bool is_power_of_2(int x)
{
	return (x & (x - 1)) == 0;
}

// Get the next highest power of 2 greater than or equal to x. Result for 0 is
// undefined.
static inline int next_power_of_2(int x)
{
	int answer;
	for (answer = 1; answer < x; answer <<= 1);
	return answer;
}

// Align a value. Alignment must be a power of 2.
static inline int align(int x, int align)
{
	return (x + align - 1) & ~(align - 1);
}

#endif
