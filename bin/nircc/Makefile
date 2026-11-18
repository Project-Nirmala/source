CC = cc
HOSTCC = cc
CFLAGS = -std=c89 -pedantic -Wall -Wextra -g
CC1SRC = $(wildcard src/*.c)
CC1OBJ = $(CC1SRC:.c=.o)
BITS ?= 64
SELFDIR = build/self
SELFOBJ = $(patsubst src/%.c,$(SELFDIR)/%.o,$(CC1SRC))

all: nircc cc

nircc: $(CC1OBJ)
	$(CC) -o $@ $(CC1OBJ)

$(CC1OBJ): src/cc.h

cc: driver/driver.c
	$(HOSTCC) -std=c99 -Wall -Wextra -O1 -o $@ driver/driver.c

clean:
	rm -f src/*.o nircc cc
	rm -rf build

test: all
	@OPT=-O0 BITS=$(BITS) sh tests/run.sh all
	@OPT=-O1 BITS=$(BITS) sh tests/run.sh all

bootstrap: all
	@BITS=$(BITS) OPT=-O0 sh tools/bootstrap.sh
	@BITS=$(BITS) OPT=-O1 sh tools/bootstrap.sh

$(SELFDIR)/%.o: src/%.c src/cc.h nircc cc
	@mkdir -p $(SELFDIR)
	@echo "  SELF $<"
	@./cc -c -m$(BITS) -o $@ $<

self: all $(SELFOBJ)
	@./cc -m$(BITS) -o $(SELFDIR)/nircc $(SELFOBJ)
	@echo "  built $(SELFDIR)/nircc with ./cc (gcc is only used for the first build)"
	@NIRCC=$(SELFDIR)/nircc BITS=$(BITS) sh tests/run.sh all

check: test self bootstrap

.PHONY: all clean test self bootstrap check
