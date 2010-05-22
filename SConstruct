env = Environment(CFLAGS="-O3")

beard_env = Environment(CFLAGS=['-ggdb3',
                                '-m32',
                                '-O3',
                                '-march=native',
                                '-U_FORTIFY_SOURCE',
                                '-Wall',
                                '-Wextra',
                                '-D_GNU_SOURCE',
                                '-pipe'])

if ARGUMENTS.get('profile', 0):
    beard_env.Append(CFLAGS="-pg -fno-inline".split())
else:
    beard_env.Append(CFLAGS="-DNDEBUG -fomit-frame-pointer".split())


lex = env.CFile("lex.yy.c", "strip.l")
env.Program("strip", lex, LIBS=["fl"])

env.Program("extract.c", LIBS=["expat"])

env.Program("convert.c")

beard_env.Program("cbeardy", ["markov.c", "stringpool.c"])
