env = Environment(CFLAGS="-O3")


lex = env.CFile("lex.yy.c", "strip.l")

env.Program("strip", lex, LIBS=["fl"])

env.Program("extract.c", LIBS=["expat"])

env.Program("convert.c")

env.Program("cbeardy", ["markov.c", "stringpool.c"])