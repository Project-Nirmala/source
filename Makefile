unq      = $(patsubst "%",%,$(1))

ifeq ($(filter clean distclean,$(MAKECMDGOALS)),)
-include .config
endif

ifneq ($(or $(SELFTEST),$(TRACE)),)
$(error SELFTEST and TRACE are configuration symbols now: run 'make menuconfig' or 'make setconfig SET="SELFTEST=y"')
endif

ifneq ($(call unq,$(CONFIG_CROSS_PREFIX)),)
CROSS ?= $(call unq,$(CONFIG_CROSS_PREFIX))
else ifeq ($(shell uname -s)-$(shell uname -m),Linux-x86_64)
CROSS ?=
else
CROSS ?= x86_64-elf-
endif

CC       := $(CROSS)gcc
LD       := $(CROSS)ld
AR       := $(CROSS)ar
ARCH     := amd64
BUILD    := build
AUTOCONF := $(BUILD)/autoconf.h
NIRCONFIG:= $(BUILD)/host/nirconfig
NCLIBS   ?= -lcurses
NCSRCS   := $(wildcard tools/nirconfig/*.c)
OPTLEVEL := $(or $(CONFIG_OPT_LEVEL),2)
DBGFLAG  := $(if $(filter y,$(CONFIG_DEBUG_INFO)),-g)
WERRFLAG := $(if $(filter y,$(CONFIG_WERROR)),-Werror)
LIMINE   ?= limine
LIMINE_BRANCH ?= $(or $(call unq,$(CONFIG_LIMINE_BRANCH)),v11.x-binary)
QEMU     ?= qemu-system-x86_64
QEMU_MEM ?= $(or $(call unq,$(CONFIG_QEMU_MEM)),256M)
BOOT_TIMEOUT ?= $(or $(CONFIG_BOOT_TIMEOUT),3)

CFLAGS   := -std=gnu11 -O$(OPTLEVEL) $(DBGFLAG) -Wall -Wextra $(WERRFLAG) \
            -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
            -mno-red-zone -mcmodel=kernel -mno-sse -mno-sse2 -mno-mmx -mno-80387 \
            -fno-omit-frame-pointer -fno-asynchronous-unwind-tables -Wno-error=unused-but-set-variable \
            -Iinclude -Isys/include -Isys/arch/$(ARCH)/include -include $(AUTOCONF)
ASFLAGS  := $(DBGFLAG) -Isys/include
LDFLAGS  := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 -z noexecstack \
            -T sys/arch/$(ARCH)/linker.ld

SRCS_C   := $(wildcard sys/kern/*.c) $(wildcard sys/vm/*.c) $(wildcard sys/fs/*.c) \
            $(wildcard sys/dev/*.c) $(wildcard sys/arch/$(ARCH)/*.c)
SRCS_S   := $(wildcard sys/arch/$(ARCH)/*.S)
OBJS     := $(patsubst %.c,$(BUILD)/%.o,$(SRCS_C)) $(patsubst %.S,$(BUILD)/%.o,$(SRCS_S))

KERNEL   := $(BUILD)/nirmala
INITRD   := $(BUILD)/initrd.tar
ISO      := $(BUILD)/nirmala.iso
QEMUCON  := -chardev stdio,id=con0,mux=on,signal=off -serial chardev:con0 -mon chardev=con0,mode=readline
DISK     ?= disk.img
DISKSIZE ?= $(or $(call unq,$(CONFIG_DISK_SIZE)),64M)
ifeq ($(CONFIG_NODISK),y)
NODISK   := 1
endif
ifndef NODISK
QEMUDISK := -drive file=$(DISK),if=none,id=disk0,format=raw \
            -device virtio-blk-pci,drive=disk0,disable-modern=on,disable-legacy=off
DISKDEP  := $(DISK)
endif
QEMUFLAGS:= -M q35 -m $(QEMU_MEM) -cdrom $(ISO) $(QEMUCON) $(QEMUDISK) -display none -no-reboot -no-shutdown

UCFLAGS  := -std=gnu11 -O$(OPTLEVEL) -Wall -Wextra $(WERRFLAG) -ffreestanding -fno-builtin \
            -fno-stack-protector -fno-pic -fno-pie \
            -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables \
            -Iinclude -Ilib/libc/include -include $(AUTOCONF)
ULDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 -z noexecstack \
            -T lib/libc/user.ld

PROGS    := sbin/init bin/login bin/sh bin/ls bin/cat bin/echo bin/vi bin/mkdir bin/rmdir bin/rm bin/cp bin/mv \
            bin/touch bin/wc bin/head bin/tail bin/sort bin/uniq bin/grep bin/tee bin/uname bin/sleep \
            bin/true bin/false bin/pwd bin/clear bin/resize bin/df bin/kill bin/test bin/date bin/ttydemo bin/utest bin/selftest
LIBC_SRC := $(wildcard lib/libc/*.c)
LIBC_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(LIBC_SRC))
LIBC_A   := $(BUILD)/lib/libc/libc.a
CRT0     := $(BUILD)/lib/libc/crt0.o
MPROGS   := bin/nirmake bin/nirld bin/nircc
ALIASES  := bin/make:bin/nirmake bin/ld:bin/nirld bin/[:bin/test
ROOTBINS := $(addprefix $(BUILD)/rootfs/,$(PROGS) $(MPROGS)) $(BUILD)/rootfs/bin/cc
ROOTETC  := $(patsubst rootfs/%,$(BUILD)/rootfs/%,$(shell find rootfs -type f)) $(BUILD)/rootfs/usr/share/doc/LICENSE \
            $(BUILD)/rootfs/usr/lib/crt0.o $(BUILD)/rootfs/usr/lib/libc.a \
            $(BUILD)/rootfs/usr/share/examples/hello/hello.o $(BUILD)/rootfs/usr/include/.installed \
            $(BUILD)/rootfs/usr/lib/nircc/include/.installed
SYSHDRS  := $(wildcard lib/libc/include/*.h lib/libc/include/sys/*.h include/sys/*.h)

.PHONY: all iso run run-gui debug clean distclean limine test test-toolchain disk-reset \
        menuconfig defconfig showconfig setconfig

all: $(KERNEL) $(INITRD)

$(NIRCONFIG): $(NCSRCS) tools/nirconfig/nc.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -std=c89 -pedantic -O1 -Wall -Wextra -o $@ $(NCSRCS) $(NCLIBS)

.config: Kconfig | $(NIRCONFIG)
	$(NIRCONFIG) -k Kconfig -c $@ old

$(AUTOCONF): .config | $(NIRCONFIG)
	@mkdir -p $(dir $@)
	$(NIRCONFIG) -k Kconfig -c .config -H $@ gen

menuconfig: .config | $(NIRCONFIG)
	$(NIRCONFIG) -k Kconfig -c .config menu

defconfig: | $(NIRCONFIG)
	$(NIRCONFIG) -k Kconfig -c .config default

showconfig: .config | $(NIRCONFIG)
	@$(NIRCONFIG) -k Kconfig -c .config list

setconfig: .config | $(NIRCONFIG)
	$(NIRCONFIG) -k Kconfig -c .config set $(SET)

$(KERNEL): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)/sys/kern/string.o: CFLAGS += -fno-tree-loop-distribute-patterns

$(BUILD)/%.o: %.c $(AUTOCONF)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S $(AUTOCONF)
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/lib/%.o $(BUILD)/bin/%.o $(BUILD)/sbin/%.o: CFLAGS := $(UCFLAGS)

$(LIBC_A): $(LIBC_OBJ)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $^

define PROG_RULE
$$(BUILD)/rootfs/$(1): $$(BUILD)/$(1)/$(notdir $(1)).o $$(CRT0) $$(LIBC_A) lib/libc/user.ld
	@mkdir -p $$(dir $$@)
	$$(LD) $$(ULDFLAGS) -o $$@ $$(CRT0) $$< $$(LIBC_A)
endef
$(foreach p,$(PROGS),$(eval $(call PROG_RULE,$(p))))

define MPROG_RULE
$(1)_OBJ := $$(patsubst %.c,$$(BUILD)/%.o,$$(wildcard $(1)/src/*.c))
$$(BUILD)/rootfs/$(1): $$($(1)_OBJ) $$(CRT0) $$(LIBC_A) lib/libc/user.ld
	@mkdir -p $$(dir $$@)
	$$(LD) $$(ULDFLAGS) -o $$@ $$(CRT0) $$($(1)_OBJ) $$(LIBC_A)
endef
$(foreach p,$(MPROGS),$(eval $(call MPROG_RULE,$(p))))

$(BUILD)/rootfs/bin/cc: $(BUILD)/bin/nircc/driver/driver.o $(CRT0) $(LIBC_A) lib/libc/user.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@ $(CRT0) $< $(LIBC_A)

$(BUILD)/rootfs/usr/lib/nircc/include/.installed: $(wildcard bin/nircc/include/*.h)
	@mkdir -p $(dir $@)
	cp bin/nircc/include/stddef.h bin/nircc/include/stdarg.h bin/nircc/include/lim.h $(dir $@)
	@touch $@

$(BUILD)/rootfs/usr/lib/crt0.o: $(CRT0)
	@mkdir -p $(dir $@)
	cp $< $@

$(BUILD)/rootfs/usr/lib/libc.a: $(LIBC_A)
	@mkdir -p $(dir $@)
	cp $< $@

$(BUILD)/rootfs/usr/share/examples/hello/hello.o: rootfs/usr/share/examples/hello/hello.c $(AUTOCONF)
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD)/rootfs/usr/include/.installed: $(SYSHDRS)
	@mkdir -p $(dir $@)/sys
	cp lib/libc/include/*.h $(dir $@)
	cp lib/libc/include/sys/*.h include/sys/*.h $(dir $@)sys/
	@touch $@

$(BUILD)/rootfs/usr/share/doc/LICENSE: LICENSE
	@mkdir -p $(dir $@)
	cp $< $@

$(BUILD)/rootfs/%: rootfs/%
	@mkdir -p $(dir $@)
	cp $< $@

$(INITRD): $(ROOTBINS) $(ROOTETC)
	@for a in $(ALIASES); do cp -f "$(BUILD)/rootfs/$${a#*:}" "$(BUILD)/rootfs/$${a%%:*}"; done
	tar --format=ustar --sort=name --owner=0 --group=0 --numeric-owner \
		-cf $@ -C $(BUILD)/rootfs $(sort $(foreach f,$(PROGS) $(MPROGS) $(patsubst rootfs/%,%,$(shell find rootfs -type f)),$(firstword $(subst /, ,$(f)))))

limine:
	@test -d $(LIMINE) || git clone --depth=1 --branch=$(LIMINE_BRANCH) \
		https://github.com/limine-bootloader/limine.git $(LIMINE)
	@$(MAKE) -C $(LIMINE)

iso: $(ISO)

$(ISO): $(KERNEL) $(INITRD) boot/limine.conf $(AUTOCONF) | limine
	rm -rf $(BUILD)/iso
	mkdir -p $(BUILD)/iso/boot/limine $(BUILD)/iso/EFI/BOOT
	cp $(KERNEL) $(BUILD)/iso/boot/nirmala
	cp $(INITRD) $(BUILD)/iso/boot/initrd.tar
	sed 's/^timeout:.*/timeout: $(BOOT_TIMEOUT)/' boot/limine.conf > $(BUILD)/iso/boot/limine/limine.conf
	cp $(LIMINE)/limine-bios.sys $(LIMINE)/limine-bios-cd.bin \
	   $(LIMINE)/limine-uefi-cd.bin $(BUILD)/iso/boot/limine/
	cp $(LIMINE)/BOOTX64.EFI $(BUILD)/iso/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(BUILD)/iso -o $@
	$(LIMINE)/limine bios-install $@

$(DISK):
	truncate -s $(DISKSIZE) $@

disk-reset:
	rm -f $(DISK)

run: $(ISO) $(DISKDEP)
	$(QEMU) $(QEMUFLAGS)

run-gui: $(ISO) $(DISKDEP)
	$(QEMU) -M q35 -m $(QEMU_MEM) -cdrom $(ISO) $(QEMUCON) $(QEMUDISK) -no-reboot -no-shutdown

debug: $(ISO) $(DISKDEP)
	$(QEMU) $(QEMUFLAGS) -s -S -d int,cpu_reset

clean:
	rm -rf $(BUILD)

distclean: clean
	rm -rf $(LIMINE) .config

HOSTCC    ?= gcc
HOSTFLAGS := -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-array-bounds -Wno-unused-parameter -Iinclude -Isys/include -Isys/arch/amd64/include

test: all
	$(HOSTCC) $(HOSTFLAGS) -DLEMON_HOST_TEST tests/lemon_test.c sys/fs/lemon_core.c -o $(BUILD)/lemon_test
	$(BUILD)/lemon_test
	$(HOSTCC) $(HOSTFLAGS) -DLEMON_HOST_TEST=0 tests/vfs_test.c sys/fs/vfs.c sys/fs/lemonfs.c sys/fs/lemon_core.c \
	    sys/fs/tarfs.c sys/dev/ramdisk.c sys/dev/blkcache.c -o $(BUILD)/vfs_test
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/vfs_test
	$(HOSTCC) $(HOSTFLAGS) -DLEMON_HOST_TEST=0 tests/persist_test.c sys/fs/vfs.c sys/fs/lemonfs.c sys/fs/lemon_core.c \
	    sys/fs/tarfs.c sys/dev/ramdisk.c sys/dev/blkcache.c -o $(BUILD)/persist_test
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/persist_test
	$(HOSTCC) $(HOSTFLAGS) -DVIRTIO_HOST_TEST -DLEMON_HOST_TEST=0 -Itests tests/blk_test.c sys/dev/virtio_blk.c \
	    sys/dev/blkcache.c sys/fs/lemon_core.c -o $(BUILD)/blk_test
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/blk_test
	$(HOSTCC) $(HOSTFLAGS) tests/fbcon_test.c sys/dev/fbcon.c sys/dev/font8x16.c -o $(BUILD)/fbcon_test
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/fbcon_test
	$(HOSTCC) -Itests/mock $(HOSTFLAGS) tests/kern_test.c sys/dev/rtc.c sys/kern/brk.c -o $(BUILD)/kern_test
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/kern_test
	$(HOSTCC) -O1 -g -fsanitize=undefined -Wall -Wextra -DMALLOC_TEST -Derrno=nm_errno -Dmalloc=nm_malloc -Dfree=nm_free \
	    -Dcalloc=nm_calloc -Drealloc=nm_realloc -Iinclude -Ilib/libc/include -c lib/libc/malloc.c -o $(BUILD)/malloc_nm.o
	$(HOSTCC) -O1 -g -fsanitize=undefined -Wno-unused-parameter tests/malloc_test.c $(BUILD)/malloc_nm.o -o $(BUILD)/malloc_test
	$(BUILD)/malloc_test
	$(HOSTCC) -O1 -g -Dregcomp=nr_regcomp -Dregexec=nr_regexec -Dregfree=nr_regfree -Dregerror=nr_regerror -Dfnmatch=nr_fnmatch \
	    -Dregex_t=nr_regex_t -Dregmatch_t=nr_regmatch_t -Iinclude -Ilib/libc/include -c lib/libc/regex.c -o $(BUILD)/nr_regex.o
	$(HOSTCC) -O1 -g -Dfnmatch=nr_fnmatch -Iinclude -Ilib/libc/include -c lib/libc/fnmatch.c -o $(BUILD)/nr_fnmatch.o
	$(HOSTCC) -O1 -g -Wno-unused-parameter tests/libc_diff_test.c $(BUILD)/nr_regex.o $(BUILD)/nr_fnmatch.o -o $(BUILD)/libc_diff
	$(BUILD)/libc_diff

test-toolchain: all
	sh tests/toolchain_test.sh
