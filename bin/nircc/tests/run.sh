#!/bin/sh

cd "$(dirname "$0")/.." || exit 1
BITS=${BITS:-64}
NIRCC=${NIRCC:-./nircc}
OPT=${OPT:-}
TMP=${TMPDIR:-/tmp}/nircc-test.$$
mkdir -p "$TMP" || exit 1
trap 'rm -rf "$TMP"' EXIT INT TERM

names=
for f in tests/*.c; do
	n=${f#tests/}
	names="$names ${n%.c}"
done

want_exit() {
	w=$(cat "tests/$1.exit" 2>/dev/null)
	echo "${w:-0}"
}

show_menu() {
	echo "Tests (bits=$BITS opt=${OPT:-default}):"
	i=0
	for n in $names; do
		i=$((i + 1))
		extra=
		[ -f "tests/$n.out" ] && extra=" + output"
		printf '  %2d) %-18s exit %s%s\n' "$i" "$n" "$(want_exit "$n")" "$extra"
	done
	echo
	printf 'Run which? number(s) or name(s), Enter = all, q = quit: '
}

resolve() {
	case $1 in
	a|all) echo "$names" ;;
	[0-9]*)
		i=0
		for n in $names; do
			i=$((i + 1))
			[ "$i" = "$1" ] && { echo "$n"; return; }
		done
		echo "run.sh: no test number $1" >&2
		return 1 ;;
	*)
		n=${1#tests/}
		n=${n%.c}
		if [ -f "tests/$n.c" ]; then
			echo "$n"
		else
			echo "run.sh: no test '$1'" >&2
			return 1
		fi ;;
	esac
}

run_one() {
	n=$1
	src=tests/$n.c
	want=$(want_exit "$n")
	if ! $NIRCC $OPT -Iinclude -m$BITS "$src" -o "$TMP/t.s" 2>"$TMP/err"; then
		echo "FAIL $n: compile error"
		sed 's/^/     /' "$TMP/err" | head -5
		return 1
	fi
	if ! cc -m$BITS -no-pie "$TMP/t.s" -o "$TMP/t" 2>"$TMP/err"; then
		echo "FAIL $n: assemble/link error"
		sed 's/^/     /' "$TMP/err" | head -5
		return 1
	fi
	"$TMP/t" >"$TMP/got.out" 2>/dev/null
	got=$?
	ok=1
	if [ "$got" != "$want" ]; then
		echo "FAIL $n: exit code want $want, got $got"
		ok=0
	fi
	if [ -f "tests/$n.out" ] && ! cmp -s "tests/$n.out" "$TMP/got.out"; then
		echo "FAIL $n: output differs (< expected, > got)"
		diff "tests/$n.out" "$TMP/got.out" | head -10 | sed 's/^/     /'
		ok=0
	fi
	[ $ok = 1 ] || return 1
	if [ -f "tests/$n.out" ]; then
		echo "ok   $n (exit $got, output matches)"
	else
		echo "ok   $n (exit $got)"
	fi
}

if [ $# -eq 0 ]; then
	if [ -t 0 ]; then
		show_menu
		read -r choice || choice=q
		case $choice in
		q|quit) exit 0 ;;
		"") choice=all ;;
		esac
		set -- $choice
	else
		set -- all
	fi
fi

chosen=
for a in "$@"; do
	r=$(resolve "$a") || exit 2
	chosen="$chosen $r"
done

pass=0
fail=0
for n in $chosen; do
	if run_one "$n"; then
		pass=$((pass + 1))
	else
		fail=$((fail + 1))
	fi
done
echo "bits=$BITS opt=${OPT:-default} pass=$pass fail=$fail"
[ "$fail" -eq 0 ]
