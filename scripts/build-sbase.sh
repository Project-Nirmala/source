#!/bin/bash
set -e
SRC=$(cd "${1:-$PWD}" && pwd)
OUT=$(mkdir -p "${2:-/tmp/nirmala-sbase}" && cd "${2:-/tmp/nirmala-sbase}" && pwd)
shift 2
TOOLS="$*"
PIN=b30fb56804bfed69b45ef0e944d2e029e4d26258
SUM=053b27edf5e6864de31513446cbe46d9252ea74c4ffece41a54a121929c5506c
if [ -z "$SBASE_SRC" ]; then
    if [ ! -f "$OUT/src/.pin-$PIN" ]; then
        rm -rf "$OUT/src"
        mkdir -p "$OUT/src"
        curl -fsSL -o "$OUT/sbase.tgz" "https://codeload.github.com/michaelforney/sbase/tar.gz/$PIN"
        echo "$SUM  $OUT/sbase.tgz" | sha256sum -c - >/dev/null
        tar xzf "$OUT/sbase.tgz" -C "$OUT/src" --strip-components=1
        touch "$OUT/src/.pin-$PIN"
    fi
    SBASE_SRC=$OUT/src
fi
SBASE_SRC=$(cd "$SBASE_SRC" && pwd)
rm -rf "$OUT/srcp"
cp -a "$SBASE_SRC" "$OUT/srcp"
cp "$SRC"/scripts/sbase-override/*.c "$OUT/srcp/"
sh "$SRC/scripts/sbase-fixmsgs.sh" "$OUT/srcp"
SBASE_SRC=$OUT/srcp
CF="-std=gnu99 -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -m64 -nostdinc -I$(gcc -print-file-name=include) -I$SBASE_SRC -I$SRC/lib/libc/include -I$SRC/include -idirafter $SRC/sys/include -D_DEFAULT_SOURCE -w"
LDF="-m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 -z noexecstack --gc-sections -s -T $SRC/lib/libc/user.ld"
rm -rf "$OUT/obj" "$OUT/bin"
mkdir -p "$OUT/obj" "$OUT/bin"
for f in "$SBASE_SRC"/libutil/*.c "$SBASE_SRC"/libutf/*.c; do
    gcc $CF -c "$f" -o "$OUT/obj/$(basename "$(dirname "$f")")_$(basename "$f" .c).o"
done
rm -f "$OUT/libsb.a"
ar rcs "$OUT/libsb.a" "$OUT"/obj/*.o
build_one() {
    gcc $CF -c "$SBASE_SRC/$1.c" -o "$OUT/obj/t_$1.o"
    ld $LDF -o "$OUT/bin/$1" "$CRT0" "$OUT/obj/t_$1.o" "$OUT/libsb.a" "$LIBC_A"
}
export -f build_one
export CF LDF OUT SBASE_SRC CRT0 LIBC_A SRC
printf '%s\n' $TOOLS | xargs -P "$(nproc)" -I{} bash -c 'build_one {}'
