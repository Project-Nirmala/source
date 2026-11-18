#!/bin/sh
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=$ROOT/build/rootfs
cd "$ROOT/bin/nirmake" && sh tests/run.sh "$B/bin/nirmake"
cd "$ROOT/bin/nirld" && LD="$B/bin/nirld" sh tests/run.sh
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cd "$T" && cp "$B"/usr/share/examples/hello/* .
PATH="$B/bin:$PATH" make LIB="$B/usr/lib" >/dev/null
out=$(./hello check)
case $out in
*"linked by nirld"*"strtod=325"*) echo "end-to-end: hello linked by nirld ran: $out" ;;
*) echo "end-to-end FAILED: $out"; exit 1 ;;
esac
cd "$T"
cat >nc.c <<'CEOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct pt { int x; int y; };
static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int main(int argc, char **argv)
{
	char *p = malloc(32);
	struct pt a;
	a.x = argc;
	a.y = fib(15);
	strcpy(p, "nircc");
	printf("%s x=%d fib=%d\n", p, a.x, a.y);
	free(p);
	return 0;
}
CEOF
"$B/bin/nircc" -c -I"$B/usr/lib/nircc/include" -I"$B/usr/include" nc.c -o nc.o
"$B/bin/nirld" -o nc "$B/usr/lib/crt0.o" nc.o -L"$B/usr/lib" -lc
out=$(./nc a)
case $out in
"nircc x=2 fib=610") echo "end-to-end: nircc compiled, nirld linked, ran: $out" ;;
*) echo "nircc end-to-end FAILED: $out"; exit 1 ;;
esac
