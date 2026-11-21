#!/bin/sh
set -e

REV=8f4a3eb042d236bf3c2af7a34c1c53d7b26dd0a9
DEST=${1:-lib/libcurses}
TMP=$(mktemp -d)

git clone -q --filter=blob:none --sparse https://github.com/NetBSD/src.git "$TMP/src"
git -C "$TMP/src" sparse-checkout set lib/libcurses lib/libterminfo usr.bin/tic share/terminfo
git -C "$TMP/src" checkout -q $REV

rm -rf "$DEST/src" "$DEST/terminfo" "$DEST/host"
mkdir -p "$DEST/src" "$DEST/terminfo" "$DEST/host"
cp "$TMP"/src/lib/libcurses/*.c "$TMP"/src/lib/libcurses/*.h "$TMP"/src/lib/libcurses/shlib_version "$TMP"/src/lib/libcurses/genfileioh.awk "$DEST/src/"
cp "$TMP"/src/lib/libterminfo/*.c "$TMP"/src/lib/libterminfo/*.h "$DEST/terminfo/"
cp "$TMP/src/usr.bin/tic/tic.c" "$TMP/src/share/terminfo/terminfo" "$DEST/host/"
rm -rf "$TMP"
