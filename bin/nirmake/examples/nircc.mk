CC = cc
HOSTCC = cc
CFLAGS = -std=c89 -pedantic -Wall -Wextra -g
CC1SRC != ls src/*.c
CC1OBJ = ${CC1SRC:.c=.o}
BITS ?= 64
SELFDIR = build/self
SELFOBJ = ${CC1SRC:T:.c=.o:S,^,${SELFDIR}/,}

.SUFFIXES: .c .o

.PHONY: all clean test self bootstrap check

all: nircc cc

nircc: ${CC1OBJ}
	${CC} -o ${.TARGET} ${CC1OBJ}

.c.o:
	${CC} ${CFLAGS} -c -o ${.TARGET} ${.IMPSRC}

${CC1OBJ}: src/cc.h

cc: driver/driver.c
	${HOSTCC} -std=c99 -Wall -Wextra -O1 -o ${.TARGET} driver/driver.c

clean:
	rm -f src/*.o nircc cc
	rm -rf build

test: all
	@OPT=-O0 BITS=${BITS} sh tests/run.sh all
	@OPT=-O1 BITS=${BITS} sh tests/run.sh all

bootstrap: all
	@BITS=${BITS} OPT=-O0 sh tools/bootstrap.sh
	@BITS=${BITS} OPT=-O1 sh tools/bootstrap.sh

.for f in ${CC1SRC:T:R}
${SELFDIR}/${f}.o: src/${f}.c src/cc.h nircc cc
	@mkdir -p ${SELFDIR}
	@echo "  SELF src/${f}.c"
	@./cc -c -m${BITS} -o ${.TARGET} src/${f}.c
.endfor

self: all ${SELFOBJ}
	@./cc -m${BITS} -o ${SELFDIR}/nircc ${SELFOBJ}
	@echo "  built ${SELFDIR}/nircc with ./cc (gcc is only used for the first build)"
	@NIRCC=${SELFDIR}/nircc BITS=${BITS} sh tests/run.sh all

check: test self bootstrap
