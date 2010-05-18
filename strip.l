%{
	#include <stdbool.h>
%} 

    int depth = 0;
    #define TOKENS_SIZE 2000
    char *tokens[TOKENS_SIZE];
    int no_of_tokens = 0;
    bool in_image = false;

    void reset_tokens()
    {
    	int i;
    	for (i = 0; i < no_of_tokens; i++)
    		free(tokens[i]);
    	no_of_tokens = 0;
    }

%%

"{{"[^}]*"}}"

"{|"

"<ref"[^\>\/]*">"[^\<]*"</ref>"
"<ref"[^\>]*"/>"

^"#REDIRECT"

"[[" depth++;
"]]" {
	depth--;
	if (depth == 0 && no_of_tokens > 0)
	{
		int i;
		for (i = 0; i < no_of_tokens - 1; i++)
			printf("%s\n", tokens[i]);		
		printf("%s", tokens[i]);
		reset_tokens();
	}
}
		

"."  if (depth == 0) printf("\n");

^\*+ {
	printf("\n");
	depth = 0;
 }

"|" reset_tokens();

^\-\-

[[:alnum:],\'\-;\(\)\\\/:]+ {
	if (depth == 0)
		printf("%s", yytext);
	else if (no_of_tokens < TOKENS_SIZE)
			tokens[no_of_tokens++] = strdup(yytext);
}


[[:space:]]+ if (depth == 0) printf("\n");

"\x1C" {
	depth=0;
	reset_tokens();
}

. 

