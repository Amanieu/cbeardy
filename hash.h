#ifndef HASH_H_
#define HASH_H_

// djb2 hash function, from http://www.cse.yorku.ca/~oz/hash.html
static inline int hash_string(const char *string)
{
	unsigned int hash = 5381;
	unsigned int c;

	while ((c = *string++))
		hash = (hash << 5) + hash + c;

	return hash;
}

// Same as above, but hashes multiple strings
static inline int hash_strings(int num_strings, const char *const *strings)
{
	unsigned int hash = 5381;
	unsigned int c;

	int i;
	for (i = 0; i < num_strings; i++) {
		const char *string = strings[i];
		while ((c = *string++))
			hash = (hash << 5) + hash + c;
	}

	return hash;
}

#endif
