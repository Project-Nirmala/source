#!/bin/sh

cd "$(dirname "$0")/.." || exit 1
BITS=${BITS:-64}
OPT=${OPT:-}
DIR=${DIR:-/tmp/nircc-boot}
FILES="util target type lex pp peep parse gen main enc"
rm -rf "$DIR"
mkdir -p "$DIR" || exit 1
[ -x ./nircc ] || make -s nircc || exit 1

build() {
	d=$DIR/stage$1
	mkdir -p "$d"
	objs=
	for f in $FILES; do
		if [ "$BITS" = 64 ]; then
			$2 $OPT -m64 -Iinclude -c "src/$f.c" -o "$d/$f.o" || return 1
		else
			$2 $OPT -m$BITS -Iinclude "src/$f.c" -o "$d/$f.s" || return 1
			as --$BITS "$d/$f.s" -o "$d/$f.o" || return 1
		fi
		objs="$objs $d/$f.o"
	done
	cc -m$BITS -no-pie $objs -o "$d/nircc"
}

step() {
	printf '%-44s' "$1"
}

done_ok() {
	echo ok
}

die() {
	echo FAILED
	[ -f "$1" ] && sed 's/^/    /' "$1" | tail -15
	echo "bootstrap failed (files kept in $DIR)"
	exit 1
}

echo "bootstrap: bits=$BITS opt=${OPT:-default}"

step "[1/4] stage 2: ./nircc builds itself"
build 2 ./nircc >"$DIR/stage2.log" 2>&1 || die "$DIR/stage2.log"
done_ok

step "[2/4] tests with the stage 2 compiler"
NIRCC=$DIR/stage2/nircc BITS=$BITS OPT=$OPT sh tests/run.sh all \
	>"$DIR/tests.log" 2>&1 || die "$DIR/tests.log"
done_ok

step "[3/4] stage 3: stage 2 builds itself"
build 3 "$DIR/stage2/nircc" >"$DIR/stage3.log" 2>&1 || die "$DIR/stage3.log"
done_ok

step "[4/4] stage 2 and stage 3 are identical"
: >"$DIR/diff.log"
X=s
[ "$BITS" = 64 ] && X=o
for f in $FILES; do
	cmp -s "$DIR/stage2/$f.$X" "$DIR/stage3/$f.$X" ||
		echo "differs: $f.$X" >>"$DIR/diff.log"
done
cmp -s "$DIR/stage2/nircc" "$DIR/stage3/nircc" ||
	echo "differs: nircc binary" >>"$DIR/diff.log"
[ ! -s "$DIR/diff.log" ] || die "$DIR/diff.log"
done_ok

echo "bootstrap OK"
