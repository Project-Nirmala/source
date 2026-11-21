#!/bin/sh
set -e

D=lib/libcurses
G=$D/gen
O=$G/out
T=$(mktemp -d)
TERMS="ansi dumb vt100 vt220 wsvt25 xterm xterm-256color linux"

mkdir -p "$O"
sh "$G/genhash.sh" "$D/terminfo/term.h" > "$O/hash.c"
awk -f "$D/src/genfileioh.awk" < "$D/src/shlib_version" > "$O/fileio.h"

cp "$D/terminfo/term.h" "$D/terminfo/term_private.h" "$D/terminfo/compile.c" "$D/terminfo/ti.c" "$D/terminfo/termcap.h" "$D/host/tic.c" "$T/"
cp "$O/hash.c" "$T/"
cc -O1 -w -include "$G/host/hostcompat.h" -I"$G/host" -I"$T" -DTERMINFO_COMPILE -DTERMINFO_COMPAT \
    "$T/tic.c" "$T/compile.c" "$T/ti.c" "$T/hash.c" "$G/host/hostshim.c" -o "$T/tic"
"$T/tic" -Sx "$D/host/terminfo" $TERMS > "$O/compiled_terms.c"
rm -rf "$T"
