#ifndef MATH_H_
#define MATH_H_

// Various math functions

// Check if a value is a power of 2
static inline int is_power_of_2(int x)
{
	return (x & (x - 1)) == 0;
}

// Align a value. Alignment must be a power of 2.
static inline int align(int x, int align)
{
	return (x + align - 1) & ~(align - 1);
}

#endif
