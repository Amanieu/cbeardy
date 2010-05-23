#ifndef MARKOV_H_
#define MARKOV_H_

// Structures for the markov export database

#include <stdint.h>

// Order of the markov model
#define MARKOV_ORDER 2

// Type of a string offset. Using 64-bit int to allow files larger than 4GB. An
// offset of -1 means a NULL string.
typedef int64_t string_offset_t;

// Type of an offset in the database file. Using 64-bit int to allow files
// larger than 4GB.
typedef int64_t markov_offset_t;

// Set structure alignment to 4 bytes
#pragma pack(push)
#pragma pack(4)

// An exit of a node in the database. Count is cumulative, so you can perform
// a binary search on the exit list when generating. The total count is the
// count on the last exit.
struct markov_export_exit_t {
	markov_offset_t node;
	int count;
};

// A node in the database
struct markov_export_node_t {
	string_offset_t strings[MARKOV_ORDER];
	int num_exits;
	struct markov_export_exit_t exits[0];
};

// Start database format
struct markov_export_start_t {
	int num_start_states;
	struct markov_export_exit_t start_states[0];
};

#pragma pack(pop)

#endif
