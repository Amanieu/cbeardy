/* Converts the 'pedia dumps from Jonny's script into a format compatible with
 * cbeardy.
 */
#include <stdio.h>

void main ()
{
	short register readDot = 0;
	short register readSpace = 0;
	
	int c;
	while ((c = getchar()) != EOF) {
		switch (c) {
			case ' ':
				if (!readSpace)
					putchar('\n');
				readSpace = 1;
				break;
			case '.':
				if (!readDot) {
					putchar('\n');
					putchar('\n');
				}
				readDot = 1;
				break;
			default:
				readSpace = 0;
				readDot = 0;
				putchar(c);
				break;
		}
	}
}
