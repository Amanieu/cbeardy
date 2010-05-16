#ifndef HASH_H_
#define HASH_H_

// djb2 hash function, from http://www.cse.yorku.ca/~oz/hash.html
int hash_string(const char *string)
{
	unsigned int hash = 5381;
	unsigned int c;

	while ((c = *string++))
		hash = (hash << 5) + hash + c;

	return hash;
}

#endif
