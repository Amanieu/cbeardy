#include <stdio.h>
#include <expat.h>
#include <expat_external.h>

#include <stdbool.h>

typedef struct Data
{
	int depth;
	bool in_text;
	int text_depth;
} Data;

void startElement(void *userData, const char *name, const char **atts)
{
	int i;
	Data *data = userData;
	/* for (i = 0; i < data->depth; i++) */
	/* 	putchar('\t'); */
	/* puts(name); */
	if ((! data->in_text) && (strcmp(name, "text") == 0))
	{
		data->in_text = true;
		data->text_depth = data->depth;
		// puts("i");
	}
	data->depth++;
}

void endElement(void *userData, const char *name)
{
	Data *data = userData;
	data->depth--;
	if (data->in_text && (data->depth == data->text_depth))
	{
		data->in_text = false;
		putchar(0x1C);
		// puts("o");
	}
	
}

void charHandler(void *userData, const char *s, int len)
{
	Data *data = userData;	
	int i;
	if (data->in_text)
		for (i = 0; i < len; i++)
			putchar(s[i]);
}

int main()
{
	char buf[BUFSIZ];
	XML_Parser parser = XML_ParserCreate(NULL);
	int done;
	Data data = {0, false, 0};

	XML_SetUserData(parser, &data);
	XML_SetElementHandler(parser, startElement, endElement);
	XML_SetCharacterDataHandler(parser, charHandler);
	do {
		size_t len = fread(buf, 1, sizeof(buf), stdin);
		done = len < sizeof(buf);
		if (!XML_Parse(parser, buf, len, done)) {
			fprintf(stderr,
			        "%s at line %d\n",
			        XML_ErrorString(XML_GetErrorCode(parser)),
			        XML_GetCurrentLineNumber(parser));
			return 1;
		}
	} while (!done);
	XML_ParserFree(parser);
	return 0;
}
