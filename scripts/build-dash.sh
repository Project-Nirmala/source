#!/bin/bash
set -e
SRC=$(cd "${1:-$PWD}" && pwd)
OUT=$(mkdir -p "${2:-/tmp/nirmala-dash}" && cd "${2:-/tmp/nirmala-dash}" && pwd)
VER=0.5.12
mkdir -p "$OUT/obj" "$OUT/libc"
cd "$OUT"
[ -d dash-$VER ] || {
    curl -sL -o dash.tgz https://archive.ubuntu.com/ubuntu/pool/main/d/dash/dash_$VER.orig.tar.gz
    tar xzf dash.tgz
}
[ -f dash-$VER/.nirmala-patched ] || {
    (cd dash-$VER && patch -p1 < "$SRC/scripts/dash-pwd-prompt.patch" >/dev/null)
    touch dash-$VER/.nirmala-patched
}
cd dash-$VER
[ -f config.h ] || { ./configure >/dev/null; make -j4 >/dev/null 2>&1 || true; }
sed -e '/HAVE_SIGSETMASK/d' -e '/HAVE_FACCESSAT/d' config.h > "$OUT/config.h"
(cd src && gcc -E -x c -include ../config.h -DJOBS=0 -DSMALL builtins.def.in | sed -e '/^#/d' -e '/^$/d' > builtins.def && sh mkbuiltins builtins.def)
CF="-std=gnu11 -O1 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -m64 -nostdinc -I $(gcc -print-file-name=include) -I$SRC/include -I$SRC/lib/libc/include -idirafter $SRC/sys/include"
if [ -z "$LIBC_A" ]; then
    for f in $SRC/lib/libc/*.c; do gcc $CF -c $f -o "$OUT/libc/$(basename $f .c).o"; done
    gcc $CF -c $SRC/lib/libc/crt0.S -o "$OUT/crt0.o"
    rm -f "$OUT/libc.a"
    ar rcs "$OUT/libc.a" "$OUT"/libc/*.o
    LIBC_A=$OUT/libc.a
    CRT0=$OUT/crt0.o
fi
DF="-Dstat64=stat -Dlstat64=lstat -Dfstat64=fstat -Dopen64=open -Dreaddir64=readdir -Ddirent64=dirent -DJOBS=0 -DSMALL -DSHELL -include $OUT/config.h -include sys/wait.h -include string.h"
rm -f "$OUT"/obj/*.o
for f in $(ls src/*.c src/bltin/*.c | grep -v -e histedit -e mkinit -e mknodes -e mksyntax -e mksignames); do
    gcc $CF $DF -I. -Isrc -Isrc/bltin -c $f -o "$OUT/obj/$(basename $f .c).o"
done
ld -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 -z noexecstack -T $SRC/lib/libc/user.ld "$CRT0" "$OUT"/obj/*.o "$LIBC_A" -o "$OUT/dash"
echo "$OUT/dash"
