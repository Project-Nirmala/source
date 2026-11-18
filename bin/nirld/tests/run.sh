#!/bin/sh
LD=${LD:-./nirld}
T=${TMPDIR:-/tmp}/nirld_test.$$
mkdir -p "$T"
trap 'rm -rf "$T"' EXIT
pass=0
fail=0

ok() {
	pass=$((pass + 1))
}

bad() {
	echo "FAIL $1: $2"
	fail=$((fail + 1))
}

expect_exit() {
	name=$1
	want=$2
	bin=$3
	"$bin"
	got=$?
	if [ "$got" = "$want" ]; then
		ok
	else
		bad "$name" "want exit $want got $got"
	fi
}

check_elf() {
	name=$1
	bin=$2
	if readelf -a "$bin" 2>&1 | grep -qi 'warning\|error'; then
		bad "$name" "readelf reports problems"
	else
		ok
	fi
}

expect_fail() {
	name=$1
	pattern=$2
	shift 2
	if "$@" 2> "$T/err"; then
		bad "$name" "link unexpectedly succeeded"
	elif grep -q "$pattern" "$T/err"; then
		ok
	else
		bad "$name" "missing diagnostic: $pattern"
	fi
}

run_arch() {
	arch=$1
	case $arch in
	x86_64) AS="as --64"; EMUL=elf_x86_64; BITS=64 ;;
	i386) AS="as --32"; EMUL=elf_i386; BITS=32 ;;
	esac
	d=tests/$arch
	for s in $d/*.s tests/rt/*_$arch.s; do
		b=$(basename "$s" .s)
		$AS "$s" -o "$T/$arch.$b.o" || bad "$arch/$b" "assemble failed"
	done
	o() { echo "$T/$arch.$1.o"; }
	out="$T/$arch.out"

	$LD -m $EMUL -o "$out" "$(o exit)" && expect_exit "$arch/exit" 42 "$out"
	check_elf "$arch/exit.elf" "$out"

	$LD -m $EMUL -o "$out" "$(o multi_a)" "$(o multi_b)" && expect_exit "$arch/multi" 25 "$out"
	check_elf "$arch/multi.elf" "$out"
	$LD -o "$out" "$(o multi_b)" "$(o multi_a)" && expect_exit "$arch/multi-reordered" 25 "$out"

	rm -f "$T/libt.a"
	ar rcs "$T/libt.a" "$(o arch_used)" "$(o arch_unused)"
	$LD -o "$out" "$(o arch_main)" -L"$T" -lt && expect_exit "$arch/archive-lib" 7 "$out"
	$LD -o "$out" "$(o arch_main)" "$T/libt.a" && expect_exit "$arch/archive-path" 7 "$out"
	$LD -o "$out" --start-group "$T/libt.a" "$(o arch_main)" --end-group && expect_exit "$arch/archive-group" 7 "$out"

	$LD -o "$out" "$(o comdat_a)" "$(o comdat_b)" && expect_exit "$arch/comdat" 2 "$out"
	$LD -o "$out" "$(o got)" && expect_exit "$arch/got" 5 "$out"
	check_elf "$arch/got.elf" "$out"
	if [ "$arch" = x86_64 ]; then want=16; else want=8; fi
	$LD -o "$out" "$(o initarr)" && expect_exit "$arch/init-array-syms" $want "$out"

	$LD -s -o "$out" "$(o exit)" && expect_exit "$arch/strip" 42 "$out"
	if readelf -S "$out" | grep -q '\.symtab'; then
		bad "$arch/strip" "symtab present"
	else
		ok
	fi

	$LD -Ttext 0x100000 -o "$out" "$(o exit)" && expect_exit "$arch/ttext" 42 "$out"
	if readelf -h "$out" | grep -q 'Entry point address:.*0x100000$'; then
		ok
	else
		bad "$arch/ttext" "entry point is not 0x100000"
	fi
	check_elf "$arch/ttext.elf" "$out"

	$LD -o "$out" "$(o multi_a)" "$(o multi_b)"
	if nm "$out" | grep -q ' T add3$'; then
		ok
	else
		bad "$arch/symtab" "add3 missing from symbol table"
	fi

	expect_fail "$arch/undefined" "undefined reference to .missing_fn'" $LD -o "$out" "$(o undef)"
	expect_fail "$arch/duplicate" "multiple definition of .foo'" $LD -o "$out" "$(o dupdef_a)" "$(o dupdef_b)"

	if command -v gcc > /dev/null 2>&1; then
		for fl in "-O0" "-O2" "-O0 -fPIC" "-O2 -fPIE" "-Os -fno-asynchronous-unwind-tables"; do
			if gcc -m$BITS -ffreestanding -fno-stack-protector $fl -c tests/c/prog.c -o "$T/gp.o" 2> /dev/null &&
			   gcc -m$BITS -ffreestanding -fno-stack-protector $fl -c tests/c/ext.c -o "$T/ge.o" 2> /dev/null; then
				rm -f "$out"
				if $LD -m $EMUL -o "$out" "$T/$arch.crt0_$arch.o" "$T/gp.o" "$T/ge.o"; then
					expect_exit "$arch/gcc[$fl]" 180 "$out"
				else
					bad "$arch/gcc[$fl]" "link failed"
				fi
			fi
		done
	fi

	CF="-m$BITS -ffreestanding -fno-stack-protector -fno-pic -fno-pie"
	if command -v gcc > /dev/null 2>&1 && gcc $CF -O2 -c tests/c/gc.c -o "$T/gc0.o" 2> /dev/null; then
		gcc $CF -O2 -ffunction-sections -fdata-sections -c tests/c/gc.c -o "$T/gc.o"
		$LD -m $EMUL -o "$T/gc_off" "$T/$arch.crt0_$arch.o" "$T/gc.o"
		$LD -m $EMUL --gc-sections -Map "$T/gc.map" -o "$out" "$T/$arch.crt0_$arch.o" "$T/gc.o"
		expect_exit "$arch/gc-run" 10 "$out"
		if nm "$out" | grep -q unused_fn; then bad "$arch/gc" "unused_fn kept"; else ok; fi
		if nm "$T/gc_off" | grep -q unused_fn; then ok; else bad "$arch/gc" "control lost unused_fn"; fi
		if grep -A20 'Discarded' "$T/gc.map" | grep -q 'text.unused_fn'; then ok; else bad "$arch/gc-map" "discarded section not listed"; fi
		if grep -q '^\.text ' "$T/gc.map"; then ok; else bad "$arch/gc-map" "no .text in map"; fi
		check_elf "$arch/gc.elf" "$out"

		gcc $CF -g -O1 -c tests/c/prog.c -o "$T/dp.o"
		gcc $CF -g -O1 -c tests/c/ext.c -o "$T/de.o"
		$LD -m $EMUL -o "$out" "$T/$arch.crt0_$arch.o" "$T/dp.o" "$T/de.o"
		expect_exit "$arch/debug-run" 180 "$out"
		check_elf "$arch/debug.elf" "$out"
		if readelf --debug-dump=info "$out" 2> /dev/null | grep -q 'prog\.c'; then ok; else bad "$arch/debug" "no DWARF for prog.c"; fi
		addr=$(nm "$out" | awk '$3 == "main" { print $1 }')
		if addr2line -e "$out" "0x$addr" | grep -q 'prog\.c'; then ok; else bad "$arch/debug-line" "addr2line cannot map main"; fi
		$LD -m $EMUL -S -o "$out" "$T/$arch.crt0_$arch.o" "$T/dp.o" "$T/de.o"
		if readelf -S "$out" | grep -q '\.debug_info'; then bad "$arch/strip-debug" "debug present"; else ok; fi

		gcc $CF -O2 -c tests/c/tls_main.c -o "$T/tm.o"
		gcc $CF -O2 -c tests/c/tls_ext.c -o "$T/te.o"
		gcc $CF -O2 -c tests/c/tls_setup.c -o "$T/ts.o"
		$LD -m $EMUL -o "$out" "$T/$arch.crt0tls_$arch.o" "$T/tm.o" "$T/te.o" "$T/ts.o"
		expect_exit "$arch/tls" 81 "$out"
		check_elf "$arch/tls.elf" "$out"
		if readelf -lW "$out" | grep -q ' TLS '; then ok; else bad "$arch/tls" "no PT_TLS"; fi
	fi

	$LD -m $EMUL --defsym foo=0x1234 --defsym bar=foo+0x10 -o "$out" "$(o exit)"
	if nm "$out" | grep -qi '0*1234 a foo' && nm "$out" | grep -qi '0*1244 a bar'; then ok; else bad "$arch/defsym" "values wrong"; fi
	$LD -m $EMUL -u getval -o "$out" "$(o exit)" "$T/libt.a"
	if nm "$out" | grep -q ' getval$'; then ok; else bad "$arch/undefined-opt" "-u did not pull the member"; fi

	$LD -m $EMUL --oformat binary -Ttext 0x1000 -o "$out" "$(o exit)"
	if [ "$(wc -c < "$out")" = 12 ]; then ok; else bad "$arch/binary" "size is not 12"; fi
	if [ "$(od -An -tx1 -N1 "$out" | tr -d ' ')" = b8 ]; then ok; else bad "$arch/binary" "first byte is not b8"; fi

	$LD -m $EMUL -o "$out" "$(o multi_a)" "$(o multi_b)"
	n3=$(readelf -lW "$out" | grep -c ' LOAD ')
	$LD -m $EMUL -z noseparate-code -o "$out" "$(o multi_a)" "$(o multi_b)"
	n2=$(readelf -lW "$out" | grep -c ' LOAD ')
	if [ "$n3" = 3 ] && [ "$n2" = 2 ]; then ok; else bad "$arch/separate-code" "LOAD count $n3/$n2"; fi
	expect_exit "$arch/noseparate-run" 25 "$out"

	$LD -m $EMUL -z execstack -o "$out" "$(o exit)"
	if readelf -lW "$out" | grep GNU_STACK | grep -q 'RWE'; then ok; else bad "$arch/execstack" "flags not RWE"; fi

	if [ -n "$NIRCC" ]; then
		rm -f "$T/librt.a"
		ar rcs "$T/librt.a" "$(o puts_$arch)" 2>/dev/null || ar rcs "$T/librt.a" "$T/$arch.puts_$arch.o"
		for f in "$NIRCC"/tests/t*.c; do
			want=$(sed -n '1s/.*expect: *\([0-9]*\).*/\1/p' "$f")
			n=$(basename "$f" .c)
			if "$NIRCC/nircc" -m$BITS "$f" -o "$T/$n.s" && $AS "$T/$n.s" -o "$T/$n.o" &&
			   $LD -m $EMUL -o "$T/$n.bin" "$T/$arch.crt0_$arch.o" "$T/$n.o" -L"$T" -lrt; then
				expect_exit "$arch/nircc/$n" "$want" "$T/$n.bin"
			else
				bad "$arch/nircc/$n" "build failed"
			fi
		done
	fi
}

run_arch x86_64
run_arch i386
echo "pass=$pass fail=$fail"
[ "$fail" -eq 0 ]
