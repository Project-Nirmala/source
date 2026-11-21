#!/bin/sh
set -e
LC_ALL=C
export LC_ALL

PROGS="sbin/init bin/login bin/sh bin/ls bin/cat bin/echo bin/vi bin/mkdir bin/rmdir bin/rm bin/cp bin/mv bin/touch bin/wc bin/head bin/tail bin/sort bin/uniq bin/grep bin/tee bin/uname bin/sleep bin/true bin/false bin/pwd bin/clear bin/resize bin/df bin/kill bin/test bin/date bin/reboot bin/id bin/chmod bin/chown bin/loadkeys bin/ttydemo bin/utest bin/selftest"
MPROGS="bin/nirmake bin/nirld bin/nircc"
CPROGS="bin/install"
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
for f in sys/kern/*.c sys/vm/*.c sys/fs/*.c sys/dev/*.c sys/net/*.c sys/arch/$ARCH/*.c; do
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
for f in lib/libc/*.c lib/libdisk/*.c; do
	if [ ! -f "$f" ]; then
		continue
	fi
	uobj "$f"
	LIBC_OBJ="$LIBC_OBJ $UOBJ"
done
add '$(LIBC_A):%s\n\t$(LOG) "AR" "$@"\n\t@mkdir -p build/lib/libc/\n\t$(Q)rm -f $@\n\t$(Q)$(AR) rcs $@%s\n\n' "$LIBC_OBJ" "$LIBC_OBJ"

CD=lib/libcurses
if [ ! -f "$CD/src/curses.c" ]; then
	echo "  FETCH libcurses" >&2
	sh scripts/vendor-curses.sh >&2
fi
CURSES_SRCS="acs.c addbytes.c addch.c addchnstr.c addnstr.c attributes.c background.c bell.c border.c box.c chgat.c clear.c clearok.c clrtobot.c clrtoeol.c color.c copywin.c cr_put.c ctrace.c cur_hash.c curs_set.c curses.c delch.c deleteln.c delwin.c echochar.c erase.c fileio.c flushok.c fullname.c getch.c getstr.c getyx.c id_subwins.c idcok.c idlok.c immedok.c inch.c inchstr.c initscr.c insch.c insdelln.c insertln.c insstr.c instr.c keyname.c keypad.c leaveok.c line.c meta.c mouse.c move.c mvwin.c newwin.c nodelay.c notimeout.c overlay.c overwrite.c pause.c putchar.c refresh.c resize.c ripoffline.c scanw.c screen.c scroll.c scrollok.c setterm.c slk.c standout.c syncok.c timeout.c toucholap.c touchwin.c tstp.c tty.c unctrl.c underscore.c version.c"
TINFO_SRCS="term.c ti.c setupterm.c curterm.c tparm.c tputs.c"
CURSES_FLAGS='$(UCFLAGS) -Wno-error -w -DDISABLE_WCHAR -I'$CD'/compat -I'$CD'/src -I'$CD'/terminfo -I'$CD'/gen/out -include '$CD'/compat/nirmala.h'
UI_FLAGS='$(UCFLAGS) -Wno-error -DDISABLE_WCHAR -I'$CD'/compat -I'$CD'/src -I'$CD'/terminfo -include '$CD'/compat/nirmala.h'

cobj() {
	COBJ=build/${1%.c}.o
	add '%s: %s $(AUTOCONF)\n\t$(LOG) "CC" "$@"\n\t@mkdir -p %s/\n\t$(Q)$(CC) %s -c %s -o $@\n\n' "$COBJ" "$1" "${COBJ%/*}" "$2" "$1"
}

CURSES_OBJ=
for f in $CURSES_SRCS; do
	cobj "$CD/src/$f" "$CURSES_FLAGS"
	CURSES_OBJ="$CURSES_OBJ $COBJ"
done
for f in $TINFO_SRCS; do
	cobj "$CD/terminfo/$f" "$CURSES_FLAGS"
	CURSES_OBJ="$CURSES_OBJ $COBJ"
done
for f in gen/out/hash.c compat/compat.c compat/printw.c; do
	cobj "$CD/$f" "$CURSES_FLAGS"
	CURSES_OBJ="$CURSES_OBJ $COBJ"
done
add 'build/lib/libcurses.a:%s\n\t$(LOG) "AR" "$@"\n\t@mkdir -p build/lib/\n\t$(Q)rm -f $@\n\t$(Q)$(AR) rcs $@%s\n\n' "$CURSES_OBJ" "$CURSES_OBJ"

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
for p in $CPROGS; do
	mobj=
	for f in $p/src/*.c; do
		if [ ! -f "$f" ]; then
			continue
		fi
		case $f in
		*/ui_curses.c)
			cobj "$f" "$UI_FLAGS"
			mobj="$mobj $COBJ"
			;;
		*)
			uobj "$f"
			mobj="$mobj $UOBJ"
			;;
		esac
	done
	t=build/rootfs/$p
	ROOTBINS="$ROOTBINS $t"
	add '%s:%s build/lib/libcurses.a $(CRT0) $(LIBC_A) lib/libc/user.ld\n\t$(LOG) "LD" "$@"\n\t@mkdir -p %s/\n\t$(Q)$(LD) $(ULDFLAGS) -o $@ $(CRT0)%s build/lib/libcurses.a $(LIBC_A)\n\n' "$t" "$mobj" "${t%/*}" "$mobj"
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

TARDIRS=$( { for p in $PROGS $MPROGS $CPROGS; do echo "${p%%/*}"; done; for f in $RFILES; do g=${f#rootfs/}; echo "${g%%/*}"; done; } | sort -u | tr '\n' ' ')

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
