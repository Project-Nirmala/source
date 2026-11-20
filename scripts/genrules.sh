#!/bin/sh
set -e
LC_ALL=C
export LC_ALL

PROGS="sbin/init bin/login bin/sh bin/ls bin/cat bin/echo bin/vi bin/mkdir bin/rmdir bin/rm bin/cp bin/mv bin/touch bin/wc bin/head bin/tail bin/sort bin/uniq bin/grep bin/tee bin/uname bin/sleep bin/true bin/false bin/pwd bin/clear bin/resize bin/df bin/kill bin/test bin/date bin/ttydemo bin/utest bin/selftest"
MPROGS="bin/nirmake bin/nirld bin/nircc"
ARCH=amd64

OUT=build/rules.mk
HDR=build/rules.hdr
BODY=build/rules.body
mkdir -p build
: > "$HDR"
: > "$BODY"

add() {
	printf "$@" >> "$BODY"
}

uobj() {
	UOBJ=build/${1%.c}.o
	add '%s: %s $(AUTOCONF)\n\t$(LOG) "CC" "$@"\n\t@mkdir -p %s/\n\t$(Q)$(CC) $(UCFLAGS) -c %s -o $@\n\n' "$UOBJ" "$1" "${UOBJ%/*}" "$1"
}

KOBJS=
for f in sys/kern/*.c sys/vm/*.c sys/fs/*.c sys/dev/*.c sys/arch/$ARCH/*.c; do
	if [ ! -f "$f" ]; then
		continue
	fi
	o=build/${f%.c}.o
	extra=
	if [ "$f" = sys/kern/string.c ]; then
		extra=' -fno-tree-loop-distribute-patterns'
	fi
	KOBJS="$KOBJS $o"
	add '%s: %s $(AUTOCONF)\n\t$(LOG) "CC" "$@"\n\t@mkdir -p %s/\n\t$(Q)$(CC) $(CFLAGS)%s -c %s -o $@\n\n' "$o" "$f" "${o%/*}" "$extra" "$f"
done
for f in sys/arch/$ARCH/*.S; do
	if [ ! -f "$f" ]; then
		continue
	fi
	o=build/${f%.S}.o
	KOBJS="$KOBJS $o"
	add '%s: %s $(AUTOCONF)\n\t$(LOG) "AS" "$@"\n\t@mkdir -p %s/\n\t$(Q)$(CC) $(ASFLAGS) -c %s -o $@\n\n' "$o" "$f" "${o%/*}" "$f"
done

add '%s: %s $(AUTOCONF)\n\t$(LOG) "AS" "$@"\n\t@mkdir -p build/lib/libc/\n\t$(Q)$(CC) $(ASFLAGS) -c %s -o $@\n\n' build/lib/libc/crt0.o lib/libc/crt0.S lib/libc/crt0.S

LIBC_OBJ=
for f in lib/libc/*.c; do
	if [ ! -f "$f" ]; then
		continue
	fi
	uobj "$f"
	LIBC_OBJ="$LIBC_OBJ $UOBJ"
done
add '$(LIBC_A):%s\n\t$(LOG) "AR" "$@"\n\t@mkdir -p build/lib/libc/\n\t$(Q)rm -f $@\n\t$(Q)$(AR) rcs $@%s\n\n' "$LIBC_OBJ" "$LIBC_OBJ"

ROOTBINS=
for p in $PROGS; do
	n=${p##*/}
	uobj "$p/$n.c"
	t=build/rootfs/$p
	ROOTBINS="$ROOTBINS $t"
	add '%s: %s $(CRT0) $(LIBC_A) lib/libc/user.ld\n\t$(LOG) "LD" "$@"\n\t@mkdir -p %s/\n\t$(Q)$(LD) $(ULDFLAGS) -o $@ $(CRT0) %s $(LIBC_A)\n\n' "$t" "$UOBJ" "${t%/*}" "$UOBJ"
done
for p in $MPROGS; do
	mobj=
	for f in $p/src/*.c; do
		if [ ! -f "$f" ]; then
			continue
		fi
		uobj "$f"
		mobj="$mobj $UOBJ"
	done
	t=build/rootfs/$p
	ROOTBINS="$ROOTBINS $t"
	add '%s:%s $(CRT0) $(LIBC_A) lib/libc/user.ld\n\t$(LOG) "LD" "$@"\n\t@mkdir -p %s/\n\t$(Q)$(LD) $(ULDFLAGS) -o $@ $(CRT0)%s $(LIBC_A)\n\n' "$t" "$mobj" "${t%/*}" "$mobj"
done
uobj bin/nircc/driver/driver.c
t=build/rootfs/bin/cc
ROOTBINS="$ROOTBINS $t"
add '%s: %s $(CRT0) $(LIBC_A) lib/libc/user.ld\n\t$(LOG) "LD" "$@"\n\t@mkdir -p %s/\n\t$(Q)$(LD) $(ULDFLAGS) -o $@ $(CRT0) %s $(LIBC_A)\n\n' "$t" "$UOBJ" "${t%/*}" "$UOBJ"

RFILES=$(find rootfs -type f | sort)
ROOTCOPY=
for f in $RFILES; do
	t=build/$f
	ROOTCOPY="$ROOTCOPY $t"
	add '%s: %s\n\t$(LOG) "CP" "$@"\n\t@mkdir -p %s/\n\t$(Q)cp %s $@\n\n' "$t" "$f" "${t%/*}" "$f"
done

SYSHDRS=
for f in lib/libc/include/*.h lib/libc/include/sys/*.h include/sys/*.h; do
	if [ -f "$f" ]; then
		SYSHDRS="$SYSHDRS $f"
	fi
done
NIRCCHDRS=
for f in bin/nircc/include/*.h; do
	if [ -f "$f" ]; then
		NIRCCHDRS="$NIRCCHDRS $f"
	fi
done

TARDIRS=$( { for p in $PROGS $MPROGS; do echo "${p%%/*}"; done; for f in $RFILES; do g=${f#rootfs/}; echo "${g%%/*}"; done; } | sort -u | tr '\n' ' ')

{
	printf 'OBJS =%s\n' "$KOBJS"
	printf 'LIBC_OBJ =%s\n' "$LIBC_OBJ"
	printf 'ROOTBINS =%s\n' "$ROOTBINS"
	printf 'ROOTCOPY =%s\n' "$ROOTCOPY"
	printf 'SYSHDRS =%s\n' "$SYSHDRS"
	printf 'NIRCCHDRS =%s\n' "$NIRCCHDRS"
	printf 'TARDIRS = %s\n' "$TARDIRS"
	printf '\n'
	cat "$BODY"
} > "$OUT"
rm -f "$HDR" "$BODY"
