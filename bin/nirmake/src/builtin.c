#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nirmake.h"

static const char *builtin_vars[] = {
    "CC=cc",
    "CFLAGS=",
    "CPPFLAGS=",
    "LDFLAGS=",
    "LDLIBS=",
    "AR=ar",
    "ARFLAGS=rv",
    "AS=as",
    "ASFLAGS=",
    "LD=ld",
    "YACC=yacc",
    "YFLAGS=",
    "LEX=lex",
    "LFLAGS=",
    "RM=rm -f",
    NULL
};

static const char *builtin_rules[] = {
    ".SUFFIXES: .out .a .o .c .y .l .s .S .sh",
    ".c.o:",
    "\t${CC} ${CFLAGS} ${CPPFLAGS} -c ${.IMPSRC}",
    ".c:",
    "\t${CC} ${CFLAGS} ${CPPFLAGS} ${LDFLAGS} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}",
    ".s.o:",
    "\t${AS} ${ASFLAGS} -o ${.TARGET} ${.IMPSRC}",
    ".S.o:",
    "\t${CC} ${CFLAGS} ${CPPFLAGS} -c ${.IMPSRC}",
    ".y.c:",
    "\t${YACC} ${YFLAGS} ${.IMPSRC}",
    "\tmv -f y.tab.c ${.TARGET}",
    ".l.c:",
    "\t${LEX} ${LFLAGS} ${.IMPSRC}",
    "\tmv -f lex.yy.c ${.TARGET}",
    ".sh:",
    "\tcp ${.IMPSRC} ${.TARGET}",
    "\tchmod a+x ${.TARGET}",
    NULL
};

static void load_list(const char **lines)
{
    Buf b;
    int i;
    buf_init(&b);
    for (i = 0; lines[i] != NULL; i++) {
        buf_adds(&b, lines[i]);
        buf_addc(&b, '\n');
    }
    parse_string(b.p, "<builtin>", 1);
    buf_free(&b);
}

void builtin_load(void)
{
    load_list(builtin_vars);
    if (!opt.no_builtin)
        load_list(builtin_rules);
}
