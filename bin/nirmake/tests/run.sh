#!/bin/sh

NM=$1
case $NM in
/*) ;;
*) NM=$(pwd)/$NM ;;
esac

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT INT TERM

pass=0
fail=0

new() {
    rm -rf "$T/work"
    mkdir "$T/work"
    cd "$T/work" || exit 1
}

mk() {
    printf '%b' "$1" > "${2:-Makefile}"
}

check() {
    if [ "$2" = "$3" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL: $1"
        echo "  want: $2"
        echo "  got:  $3"
    fi
}

has() {
    case $3 in
    *"$2"*) pass=$((pass + 1)) ;;
    *)
        fail=$((fail + 1))
        echo "FAIL: $1"
        echo "  want substring: $2"
        echo "  got: $3"
        ;;
    esac
}

new
mk 'all:\n\t@echo hi\n'
check basic "hi" "$("$NM" 2>&1)"

new
mk 'A = 1\nB = $(A)\nC := $(A)\nA = 2\nall:\n\t@echo $(B) $(C)\n'
check lazy-vs-immediate "2 1" "$("$NM" 2>&1)"

new
mk 'X ?= a\nX ?= b\nY = 1\nY += 2\nall:\n\t@echo $(X) $(Y)\n'
check cond-and-append "a 1 2" "$("$NM" 2>&1)"

new
mk 'X = file\nall:\n\t@echo $(X)\n'
check cmdline-override "cmd" "$("$NM" X=cmd 2>&1)"
check env-default "file" "$(X=env "$NM" 2>&1)"
check env-override "env" "$(X=env "$NM" -e 2>&1)"

new
mk 'F = src/a.c src/b.h lib/c.c\nall:\n\t@echo "$(F:M*.c)"\n\t@echo "$(F:N*.c)"\n\t@echo "$(F:T)"\n\t@echo "$(F:H)"\n\t@echo "$(F:R)"\n\t@echo "$(F:E)"\n\t@echo "$(F:S/.c/.o/)"\n\t@echo "$(F:O)"\n\t@echo "$(F:M*.c:T:tu)"\n\t@echo "$(F:T:.c=.o)"\n\t@echo "$(F:@f@[$f]@)"\n\t@echo "$(F:C/\\.c$/.o/)"\n\t@echo "$(UNDEF:Ufallback)"\n\t@echo "$(F:Dyes)"\n\t@echo "$(F:[#])"\n\t@echo "$(F:[2])"\n\t@echo "$(F:T:%.c=%.x)"\n'
want="src/a.c lib/c.c
src/b.h
a.c b.h c.c
src src lib
src/a src/b lib/c
c h c
src/a.o src/b.h lib/c.o
lib/c.c src/a.c src/b.h
A.C C.C
a.o b.h c.o
[src/a.c] [src/b.h] [lib/c.c]
src/a.o src/b.h lib/c.o
fallback
yes
3
src/b.h
a.x b.h c.x"
check modifiers "$want" "$("$NM" 2>&1)"

new
mk 'X = a\n.if ${X} == "a"\nR1 = eq\n.else\nR1 = ne\n.endif\n.if defined(NOPE) || !defined(X)\nR2 = bad\n.elif ${X:Mb}\nR2 = bad2\n.elif 3 > 2 && exists(Makefile)\nR2 = good\n.else\nR2 = bad3\n.endif\n.ifndef NOPE\nR3 = ok\n.endif\n.if empty(NOPE)\nR4 = empty\n.endif\nall:\n\t@echo $(R1) $(R2) $(R3) $(R4)\n'
check conditionals "eq good ok empty" "$("$NM" 2>&1)"

new
mk '.for i in a b c\nL += ${i}\n.endfor\n.for t in x y\n${t}:\n\t@echo built ${t}\n.endfor\nall: x y\n\t@echo $(L)\n'
check for-loop "$(printf 'built x\nbuilt y\na b c')" "$("$NM" all 2>&1)"

new
mk '.SUFFIXES: .in .out\n.in.out:\n\t@echo "convert $< to $@ ($*)"; cp $< $@\n'
echo data > a.in
check suffix-rule "convert a.in to a.out (a)" "$("$NM" a.out 2>&1)"
has suffix-uptodate "up to date" "$("$NM" a.out 2>&1)"

new
mk '%.out: %.in\n\t@echo "pat $< $@ $*"; cp $< $@\nall: a.out\n'
echo data > a.in
check meta-rule "pat a.in a.out a" "$("$NM" 2>&1)"

new
mk 't: s\n\t@echo build; touch t\n'
touch s
check build-first "build" "$("$NM" 2>&1)"
has build-uptodate "up to date" "$("$NM" 2>&1)"
touch -t 203001010000 s
check rebuild-newer "build" "$("$NM" 2>&1)"

new
mk 't:: a\n\t@echo one\nt:: b\n\t@echo two\n'
touch a b
check double-colon "$(printf 'one\ntwo')" "$("$NM" 2>&1)"

new
mk 'all:\n\t@false\n'
"$NM" > out.txt 2>&1
check error-status 1 $?
has error-message "Error code 1" "$(cat out.txt)"

new
mk 'all:\n\t-false\n\t@echo after\n'
out=$("$NM" 2>&1)
check ignore-status 0 $?
has ignore-message "(ignored)" "$out"
has ignore-continues "after" "$out"

new
mk 'all: a b\na:\n\t@false\nb:\n\t@echo bbb\n'
out=$("$NM" -k 2>&1)
has keep-going "bbb" "$out"
"$NM" -k > /dev/null 2>&1
check keep-going-status 1 $?

new
mk 'all:\n\techo hi > f\n'
out=$("$NM" -n 2>&1)
check dry-run "echo hi > f" "$out"
check dry-run-no-file 0 "$([ -e f ] && echo 1 || echo 0)"

new
mk '.PHONY: clean\nclean:\n\t@echo cleaned\n'
touch clean
check phony "cleaned" "$("$NM" clean 2>&1)"

new
mk 'all: x y\n\t@echo "$@ [$<] [$^] [$?]"\n'
touch x y
check auto-vars "all [x] [x y] [x y]" "$("$NM" 2>&1)"

new
mk 'V = none\n.include "inc.mk"\n-include nothere.mk\n.sinclude "nothere2.mk"\nall:\n\t@echo $(V)\n'
mk 'V = fromfile\n' inc.mk
check include "fromfile" "$("$NM" 2>&1)"

new
mk 'all:\n\t@$(MAKE) -f sub.mk\n'
mk 'all:\n\t@echo "sub $(V)"\n' sub.mk
check sub-make-vars "sub cli" "$("$NM" V=cli 2>&1)"
has sub-make-dry-run 'echo "sub "' "$("$NM" -n 2>&1)"

new
mk 'all: a b\na:\n\t@sleep 1; echo a\nb:\n\t@sleep 1; echo b\n'
s=$(date +%s%N)
"$NM" -j2 > /dev/null 2>&1
e=$(date +%s%N)
el=$(((e - s) / 1000000))
check parallel-speed 1 "$([ "$el" -lt 1800 ] && echo 1 || echo 0)"

new
mk 'a: b\nb: a\n\t@echo ok\n'
has cycle "Graph cycles" "$(timeout 5 "$NM" 2>&1)"

new
mk '.DEFAULT:\n\t@echo "default $@"\n'
check default-rule "default nosuch" "$("$NM" nosuch 2>&1)"

new
mk 'all: nosuch\n\t@echo x\n'
out=$("$NM" 2>&1)
has missing-target "don't know how to make nosuch" "$out"
"$NM" > /dev/null 2>&1
check missing-status 2 $?

new
mk 'X = 1 2\nall:\n\t@echo\n'
check query-var "1 2" "$("$NM" -V X 2>&1)"

new
mk 'D != echo shell\nE = ${:!echo hi!}\nCMD = echo hey\nall:\n\t@echo "$(D) $(E) $(CMD:sh)"\n'
check shell-assign "shell hi hey" "$("$NM" 2>&1)"

new
mk 'A = one \\\n    two # comment\nB = x\\#y\nall:\n\t@echo "$(A)|$(B)"\n'
check continuation-comment "one  two|x#y" "$("$NM" 2>&1)"

new
mkdir sub
mk 'all:\n\t@echo insub\n' sub/Makefile
check chdir "insub" "$("$NM" -C sub 2>&1)"

new
mk 't: s\n\t@touch t\n'
touch s
"$NM" -q > /dev/null 2>&1
check question-out-of-date 1 $?
"$NM" > /dev/null 2>&1
"$NM" -q > /dev/null 2>&1
check question-up-to-date 0 $?

new
mk 't: s\n\t@echo build\n'
touch s
out=$("$NM" -t 2>&1)
check touch "touch t" "$out"
check touch-created 1 "$([ -e t ] && echo 1 || echo 0)"

new
mk '.BEGIN:\n\t@echo begin\n.END:\n\t@echo end\nall:\n\t@echo mid\n'
check begin-end "$(printf 'begin\nmid\nend')" "$("$NM" 2>&1)"

new
mk 'all:\n\techo hi\n'
check silent-flag "hi" "$("$NM" -s 2>&1)"
check echo-command "$(printf 'echo hi\nhi')" "$("$NM" 2>&1)"

new
mk '.c.o:\n\t@echo custom\nall:\n\t@echo default\n'
check default-goal "default" "$("$NM" 2>&1)"

new
mk 'CC = true\nall: t.o\n'
echo 'int x;' > t.c
out=$("$NM" 2>&1)
check builtin-rule "true   -c t.c" "$out"

new
mk 'OBJS = a.o b.o\nall: ${OBJS}\n\t@echo ${OBJS:.o=.x}\n%.o:\n\t@touch $@\n'
check pattern-no-source "a.x b.x" "$("$NM" 2>&1)"

echo
echo "passed: $pass failed: $fail"
[ "$fail" -eq 0 ]
