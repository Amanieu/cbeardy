/* Converts the 'pedia dumps from Jonny's script into a format compatible with
 * cbeardy.
 */
#include <stdio.h>
#include <stdbool.h>

int main(void)
{
	bool readDot = false;
	bool readSpace = false;

	int c;
	while ((c = getchar()) != EOF) {
		switch (c) {
			case ' ':
				if (!readSpace)
					putchar('\n');
				readSpace = true;
				break;
			case '.':
				if (!readDot) {
					putchar('\n');
					putchar('\n');
				}
				readDot = true;
				break;
			default:
				readSpace = readDot = false;
				putchar(c);
				break;
		}
	}

	return 0;
}
