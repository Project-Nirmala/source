#!/bin/sh

cd "$(dirname "$0")/.." || exit 1
NIRCC=${NIRCC:-./nircc}
INC=${MUSL_INC:-/usr/include/x86_64-linux-musl}
TMP=${TMPDIR:-/tmp}/nircc-musl.$$
mkdir -p "$TMP" || exit 1
trap 'rm -rf "$TMP"' EXIT INT TERM
[ -d "$INC" ] || { echo "musl.sh: no musl headers in $INC (set MUSL_INC)"; exit 1; }

ok=0
bad=0
for h in $(cd "$INC" && find . -name '*.h' -not -path './bits/*' | sed 's|^\./||' | sort); do
	printf '#include <%s>\nint main(void) { return 0; }\n' "$h" > "$TMP/h.c"
	if err=$($NIRCC -I"$INC" -m64 "$TMP/h.c" -o "$TMP/h.s" 2>&1); then
		ok=$((ok + 1))
	else
		bad=$((bad + 1))
		echo "FAIL $h: $(echo "$err" | head -1)"
	fi
done
echo "headers: ok=$ok fail=$bad"

if [ -n "$MUSL_LIB" ]; then
	$NIRCC -I"$INC" -m64 tests/musl/e2e.c -o "$TMP/e.s" &&
	as --64 "$TMP/e.s" -o "$TMP/e.o" &&
	ld -static -o "$TMP/e" "$MUSL_LIB/crt1.o" "$MUSL_LIB/crti.o" "$TMP/e.o" \
		"$MUSL_LIB/libc.a" "$MUSL_LIB/crtn.o" &&
	"$TMP/e" > "$TMP/e.out" &&
	grep -q '^1 3 5 7 9$' "$TMP/e.out" && grep -q '^sum=90$' "$TMP/e.out" &&
	echo "e2e: ok" || { echo "e2e: FAILED"; exit 1; }
fi
exit 0
