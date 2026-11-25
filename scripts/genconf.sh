#!/bin/sh
set -e

OUT=build/config.mk
mkdir -p build

if [ -n "${SELFTEST:-}${TRACE:-}" ]; then
	echo "Makefile: SELFTEST and TRACE are configuration symbols now: run 'make menuconfig' or 'make setconfig SET=\"SELFTEST=y\"'" >&2
	exit 1
fi

cfg() {
	sed -n -e "s/^CONFIG_$1=\"\(.*\)\"\$/\1/p" -e "s/^CONFIG_$1=\([^\"].*\)\$/\1/p" .config 2>/dev/null | head -n 1
}

pick() {
	if [ -n "$1" ]; then
		printf '%s' "$1"
	elif [ -n "$2" ]; then
		printf '%s' "$2"
	else
		printf '%s' "$3"
	fi
}

cross_cfg=$(cfg CROSS_PREFIX)
if [ -n "${CROSS:-}" ]; then
	cross=$CROSS
elif [ -n "$cross_cfg" ]; then
	cross=$cross_cfg
elif [ "$(uname -s)-$(uname -m)" = Linux-x86_64 ]; then
	cross=
else
	cross=x86_64-elf-
fi

optlevel=$(cfg OPT_LEVEL)
optlevel=${optlevel:-2}
dbgflag=
if [ "$(cfg DEBUG_INFO)" = y ]; then
	dbgflag=-g
fi
werrflag=
if [ "$(cfg WERROR)" = y ]; then
	werrflag=-Werror
fi

limine=${LIMINE:-limine}
limine_branch=$(pick "${LIMINE_BRANCH:-}" "$(cfg LIMINE_BRANCH)" v11.x-binary)
qemu=${QEMU:-qemu-system-x86_64}
qemu_mem=$(pick "${QEMU_MEM:-}" "$(cfg QEMU_MEM)" 256M)
boot_timeout=$(pick "${BOOT_TIMEOUT:-}" "$(cfg BOOT_TIMEOUT)" 3)
disk=${DISK:-disk.img}
disksize=$(pick "${DISKSIZE:-}" "$(cfg DISK_SIZE)" 64M)
qsata=
if [ "$(cfg QEMU_SATA)" = y ]; then
	qsata=1
fi
qnvme=
if [ "$(cfg QEMU_NVME)" = y ]; then
	qnvme=1
fi
hostcc=${HOSTCC:-gcc}
cols=$(tput cols 2>/dev/null || echo 80)
rows=$(tput lines 2>/dev/null || echo 24)

nodisk=
if [ "$(cfg NODISK)" = y ] || [ -n "${NODISK:-}" ]; then
	nodisk=1
fi

{
	printf 'CROSS = %s\n' "$cross"
	printf 'OPTLEVEL = %s\n' "$optlevel"
	printf 'DBGFLAG = %s\n' "$dbgflag"
	printf 'WERRFLAG = %s\n' "$werrflag"
	printf 'LIMINE = %s\n' "$limine"
	printf 'LIMINE_BRANCH = %s\n' "$limine_branch"
	printf 'QEMU = %s\n' "$qemu"
	printf 'QEMU_MEM = %s\n' "$qemu_mem"
	printf 'BOOT_TIMEOUT = %s\n' "$boot_timeout"
	printf 'DISK = %s\n' "$disk"
	printf 'DISKSIZE = %s\n' "$disksize"
	printf 'HOSTCC = %s\n' "$hostcc"
	printf 'COLS = %s\n' "$cols"
	printf 'ROWS = %s\n' "$rows"
	if [ "${V:-}" = 1 ]; then
		cat <<'EOF'
Q =
LOG = @true
EOF
	else
		cat <<'EOF'
Q = @
LOG = @printf "  %-5s %s\n"
EOF
	fi
	if [ -n "$nodisk" ]; then
		printf 'QEMUDISK =\nDISKDEP =\n'
	elif [ -n "$qsata" ]; then
		cat <<'EOF'
QEMUDISK = -drive file=$(DISK),if=none,id=disk0,format=raw \
           -device ide-hd,drive=disk0,bus=ide.0
DISKDEP = $(DISK)
EOF
	elif [ -n "$qnvme" ]; then
		cat <<'EOF'
QEMUDISK = -drive file=$(DISK),if=none,id=disk0,format=raw \
           -device nvme,drive=disk0,serial=nirmala0
DISKDEP = $(DISK)
EOF
	else
		cat <<'EOF'
QEMUDISK = -drive file=$(DISK),if=none,id=disk0,format=raw \
           -device virtio-blk-pci,drive=disk0,disable-modern=on,disable-legacy=off
DISKDEP = $(DISK)
EOF
	fi
	cat <<'EOF'
CC = $(CROSS)gcc
LD = $(CROSS)ld
AR = $(CROSS)ar
ARCH = amd64
BUILD = build
AUTOCONF = $(BUILD)/autoconf.h
NIRCONFIG = $(BUILD)/host/nirconfig
KERNEL = $(BUILD)/nirmala
INITRD = $(BUILD)/initrd.tar
ISO = $(BUILD)/nirmala.iso
LIBC_A = $(BUILD)/lib/libc/libc.a
CRT0 = $(BUILD)/lib/libc/crt0.o
CFLAGS = -std=gnu11 -O$(OPTLEVEL) $(DBGFLAG) -Wall -Wextra $(WERRFLAG) \
         -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
         -mno-red-zone -mcmodel=kernel -mno-sse -mno-sse2 -mno-mmx -mno-80387 \
         -fno-omit-frame-pointer -fno-asynchronous-unwind-tables -Wno-error=unused-but-set-variable \
         -Iinclude -Isys/include -Isys/arch/$(ARCH)/include -include $(AUTOCONF)
ASFLAGS = $(DBGFLAG) -Isys/include
LDFLAGS = -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 -z noexecstack \
          -T sys/arch/$(ARCH)/linker.ld
UCFLAGS = -std=gnu11 -O$(OPTLEVEL) -Wall -Wextra $(WERRFLAG) -ffreestanding -fno-builtin \
          -fno-stack-protector -fno-pic -fno-pie \
          -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables \
          -Iinclude -Ilib/libc/include -include $(AUTOCONF)
ULDFLAGS = -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 -z noexecstack \
           -T lib/libc/user.ld
QEMUCON = -chardev stdio,id=con0,mux=on,signal=off -serial chardev:con0 \
          -object monitor-hmp,id=mon0,chardev=con0
QEMUFLAGS = -M q35 -m $(QEMU_MEM) -cdrom $(ISO) $(QEMUCON) $(QEMUDISK) \
            -display none -no-reboot -no-shutdown
HOSTFLAGS = -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-array-bounds -Wno-unused-parameter -Iinclude -Isys/include -Isys/arch/amd64/include
EOF
} > "$OUT"
