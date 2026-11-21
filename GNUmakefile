unq      = $(patsubst "%",%,$(1))

ifeq ($(V),1)
  Q   :=
  LOG  = @true
else
  Q   := @
  LOG  = @printf "  %-5s %s\n"
endif

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
CMDLINE  := $(call unq,$(CONFIG_KERNEL_CMDLINE))
LIMINE_BRANCH ?= $(or $(call unq,$(CONFIG_LIMINE_BRANCH)),v11.x-binary)
QEMU     ?= qemu-system-x86_64
QEMU_MEM ?= $(or $(call unq,$(CONFIG_QEMU_MEM)),256M)
BOOT_TIMEOUT ?= $(or $(call unq,$(CONFIG_BOOT_TIMEOUT)),3)
CFLAGS   := -std=gnu11 -O$(OPTLEVEL) $(DBGFLAG) -Wall -Wextra $(WERRFLAG) \
            -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
            -mno-red-zone -mcmodel=kernel -mno-sse -mno-sse2 -mno-mmx -mno-80387 \
            -fno-omit-frame-pointer -fno-asynchronous-unwind-tables -Wno-error=unused-but-set-variable \
            -Iinclude -Isys/include -Isys/arch/$(ARCH)/include -include $(AUTOCONF)
ASFLAGS  := $(DBGFLAG) -Isys/include
LDFLAGS  := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 -z noexecstack \
            -T sys/arch/$(ARCH)/linker.ld

SRCS_C   := $(wildcard sys/kern/*.c) $(wildcard sys/vm/*.c) $(wildcard sys/fs/*.c) \
            $(wildcard sys/dev/*.c) $(wildcard sys/net/*.c) $(wildcard sys/arch/$(ARCH)/*.c)
SRCS_S   := $(wildcard sys/arch/$(ARCH)/*.S)
OBJS     := $(patsubst %.c,$(BUILD)/%.o,$(SRCS_C)) $(patsubst %.S,$(BUILD)/%.o,$(SRCS_S))

KERNEL   := $(BUILD)/nirmala
INITRD   := $(BUILD)/initrd.tar
ISO      := $(BUILD)/nirmala.iso
COLS     := $(shell tput cols 2>/dev/null || echo 80)
ROWS     := $(shell tput lines 2>/dev/null || echo 24)

QEMUCON  := -chardev stdio,id=con0,mux=on,signal=off -serial chardev:con0 \
            -object monitor-hmp,id=mon0,chardev=con0
DISK     ?= disk.img
DISKSIZE ?= $(or $(call unq,$(CONFIG_DISK_SIZE)),64M)
ifeq ($(CONFIG_NODISK),y)
NODISK   := 1
endif
ifndef NODISK
ifeq ($(CONFIG_QEMU_SATA),y)
QEMUDISK := -drive file=$(DISK),if=none,id=disk0,format=raw \
            -device ide-hd,drive=disk0,bus=ide.0
else
QEMUDISK := -drive file=$(DISK),if=none,id=disk0,format=raw \
            -device virtio-blk-pci,drive=disk0,disable-modern=on,disable-legacy=off
endif
DISKDEP  := $(DISK)
endif
QEMUNET  := -netdev user,id=net0 -device virtio-net-pci,netdev=net0,disable-modern=on,disable-legacy=off
QEMUFLAGS:= -M q35 -m $(QEMU_MEM) -cdrom $(ISO) $(QEMUCON) $(QEMUDISK) $(QEMUNET) \
            -display none -no-reboot -no-shutdown

UCFLAGS  := -std=gnu11 -O$(OPTLEVEL) -Wall -Wextra $(WERRFLAG) -ffreestanding -fno-builtin \
            -fno-stack-protector -fno-pic -fno-pie \
            -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables \
            -Iinclude -Ilib/libc/include -idirafter sys/include -include $(AUTOCONF)
ULDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 -z noexecstack \
            -T lib/libc/user.ld

PROGS    := sbin/init bin/login bin/sh bin/ls bin/cat bin/echo bin/vi bin/mkdir bin/rmdir bin/rm bin/cp bin/mv \
            bin/touch bin/wc bin/head bin/tail bin/sort bin/uniq bin/grep bin/tee bin/uname bin/sleep \
            bin/true bin/false bin/pwd bin/clear bin/resize bin/df bin/kill bin/test bin/date bin/ttydemo bin/utest bin/selftest bin/lsblk bin/dd bin/sync bin/gpt bin/fat bin/wget bin/ping
LIBC_SRC := $(wildcard lib/libc/*.c lib/libdisk/*.c)
LIBC_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(LIBC_SRC))
LIBC_A   := $(BUILD)/lib/libc/libc.a
CRT0     := $(BUILD)/lib/libc/crt0.o
MPROGS   := bin/nirmake bin/nirld bin/nircc
CPROGS   := bin/install
ALIASES  := bin/make:bin/nirmake bin/ld:bin/nirld bin/[:bin/test bin/poweroff:bin/reboot bin/halt:bin/reboot
ROOTBINS := $(addprefix $(BUILD)/rootfs/,$(PROGS) $(MPROGS) $(CPROGS)) $(BUILD)/rootfs/bin/cc
ROOTETC  := $(patsubst rootfs/%,$(BUILD)/rootfs/%,$(shell find rootfs -type f)) $(BUILD)/rootfs/usr/share/doc/LICENSE \
            $(BUILD)/rootfs/usr/lib/crt0.o $(BUILD)/rootfs/usr/lib/libc.a \
            $(BUILD)/rootfs/usr/share/examples/hello/hello.o $(BUILD)/rootfs/usr/include/.installed \
            $(BUILD)/rootfs/usr/lib/nircc/include/.installed
SYSHDRS  := $(wildcard lib/libc/include/*.h lib/libc/include/sys/*.h include/sys/*.h)

.PHONY: all iso run run-gui debug clean distclean limine test test-toolchain disk-reset \
        menuconfig defconfig showconfig setconfig

all: $(KERNEL) $(INITRD)

$(NIRCONFIG): $(NCSRCS) tools/nirconfig/nc.h
	$(LOG) "HOST" "$@"
	@mkdir -p $(dir $@)
	$(Q)$(HOSTCC) -std=c89 -pedantic -O1 -Wall -Wextra -o $@ $(NCSRCS) $(NCLIBS)

.config: Kconfig | $(NIRCONFIG)
	$(LOG) "CONF" "$@"
	$(Q)$(NIRCONFIG) -k Kconfig -c $@ old

$(AUTOCONF): .config | $(NIRCONFIG)
	$(LOG) "GEN" "$@"
	@mkdir -p $(dir $@)
	$(Q)$(NIRCONFIG) -k Kconfig -c .config -H $@ gen

menuconfig: .config | $(NIRCONFIG)
	$(Q)$(NIRCONFIG) -k Kconfig -c .config menu

defconfig: | $(NIRCONFIG)
	$(LOG) "CONF" ".config"
	$(Q)$(NIRCONFIG) -k Kconfig -c .config default

showconfig: .config | $(NIRCONFIG)
	@$(NIRCONFIG) -k Kconfig -c .config list

setconfig: .config | $(NIRCONFIG)
	$(Q)$(NIRCONFIG) -k Kconfig -c .config set $(SET)

$(KERNEL): $(OBJS)
	$(LOG) "LD" "$@"
	$(Q)$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)/sys/kern/string.o: CFLAGS += -fno-tree-loop-distribute-patterns

$(BUILD)/%.o: %.c $(AUTOCONF)
	$(LOG) "CC" "$@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S $(AUTOCONF)
	$(LOG) "AS" "$@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/lib/%.o $(BUILD)/bin/%.o $(BUILD)/sbin/%.o: CFLAGS := $(UCFLAGS)

$(LIBC_A): $(LIBC_OBJ)
	$(LOG) "AR" "$@"
	@mkdir -p $(dir $@)
	$(Q)rm -f $@
	$(Q)$(AR) rcs $@ $^

CURSES_DIR   := lib/libcurses
ifeq ($(wildcard $(CURSES_DIR)/src/curses.c),)
$(info FETCH    libcurses)
$(shell sh scripts/vendor-curses.sh >&2)
endif
CURSES_SRCS  := acs.c addbytes.c addch.c addchnstr.c addnstr.c attributes.c background.c bell.c border.c box.c chgat.c clear.c clearok.c clrtobot.c clrtoeol.c color.c copywin.c cr_put.c ctrace.c cur_hash.c curs_set.c curses.c delch.c deleteln.c delwin.c echochar.c erase.c fileio.c flushok.c fullname.c getch.c getstr.c getyx.c id_subwins.c idcok.c idlok.c immedok.c inch.c inchstr.c initscr.c insch.c insdelln.c insertln.c insstr.c instr.c keyname.c keypad.c leaveok.c line.c meta.c mouse.c move.c mvwin.c newwin.c nodelay.c notimeout.c overlay.c overwrite.c pause.c putchar.c refresh.c resize.c ripoffline.c scanw.c screen.c scroll.c scrollok.c setterm.c slk.c standout.c syncok.c timeout.c toucholap.c touchwin.c tstp.c tty.c unctrl.c underscore.c version.c
TINFO_SRCS   := term.c ti.c setupterm.c curterm.c tparm.c tputs.c
CURSES_OBJ   := $(addprefix $(BUILD)/$(CURSES_DIR)/src/,$(CURSES_SRCS:.c=.o)) \
                $(addprefix $(BUILD)/$(CURSES_DIR)/terminfo/,$(TINFO_SRCS:.c=.o)) \
                $(BUILD)/$(CURSES_DIR)/gen/out/hash.o $(BUILD)/$(CURSES_DIR)/compat/compat.o $(BUILD)/$(CURSES_DIR)/compat/printw.o
CURSES_A     := $(BUILD)/lib/libcurses.a
CURSES_FLAGS := $(filter-out -Werror,$(UCFLAGS)) -w -DDISABLE_WCHAR -I$(CURSES_DIR)/compat -I$(CURSES_DIR)/src \
                -I$(CURSES_DIR)/terminfo -I$(CURSES_DIR)/gen/out -include $(CURSES_DIR)/compat/nirmala.h

$(CURSES_OBJ): CFLAGS := $(CURSES_FLAGS)
$(CURSES_OBJ): $(wildcard $(CURSES_DIR)/compat/*.h $(CURSES_DIR)/compat/sys/*.h)

$(CURSES_A): $(CURSES_OBJ)
	$(LOG) "AR" "$@"
	@mkdir -p $(dir $@)
	$(Q)rm -f $@
	$(Q)$(AR) rcs $@ $^

define PROG_RULE
$$(BUILD)/rootfs/$(1): $$(BUILD)/$(1)/$(notdir $(1)).o $$(CRT0) $$(LIBC_A) lib/libc/user.ld
	$$(LOG) "LD" "$$@"
	@mkdir -p $$(dir $$@)
	$$(Q)$$(LD) $$(ULDFLAGS) -o $$@ $$(CRT0) $$< $$(LIBC_A)
endef
$(foreach p,$(PROGS),$(eval $(call PROG_RULE,$(p))))

define CPROG_RULE
$(1)_OBJ := $$(patsubst %.c,$$(BUILD)/%.o,$$(wildcard $(1)/src/*.c))
$$(BUILD)/rootfs/$(1): $$($(1)_OBJ) $$(CRT0) $$(CURSES_A) $$(LIBC_A) lib/libc/user.ld
	$$(LOG) "LD" "$$@"
	@mkdir -p $$(dir $$@)
	$$(Q)$$(LD) $$(ULDFLAGS) -o $$@ $$(CRT0) $$($(1)_OBJ) $$(CURSES_A) $$(LIBC_A)
endef
$(foreach p,$(CPROGS),$(eval $(call CPROG_RULE,$(p))))
$(BUILD)/bin/install/src/ui_curses.o: CFLAGS := $(filter-out -Werror,$(UCFLAGS)) -DDISABLE_WCHAR -I$(CURSES_DIR)/compat \
                                                 -I$(CURSES_DIR)/src -I$(CURSES_DIR)/terminfo -include $(CURSES_DIR)/compat/nirmala.h

define MPROG_RULE
$(1)_OBJ := $$(patsubst %.c,$$(BUILD)/%.o,$$(wildcard $(1)/src/*.c))
$$(BUILD)/rootfs/$(1): $$($(1)_OBJ) $$(CRT0) $$(LIBC_A) lib/libc/user.ld
	$$(LOG) "LD" "$$@"
	@mkdir -p $$(dir $$@)
	$$(Q)$$(LD) $$(ULDFLAGS) -o $$@ $$(CRT0) $$($(1)_OBJ) $$(LIBC_A)
endef
$(foreach p,$(MPROGS),$(eval $(call MPROG_RULE,$(p))))

$(BUILD)/rootfs/bin/cc: $(BUILD)/bin/nircc/driver/driver.o $(CRT0) $(LIBC_A) lib/libc/user.ld
	$(LOG) "LD" "$@"
	@mkdir -p $(dir $@)
	$(Q)$(LD) $(ULDFLAGS) -o $@ $(CRT0) $< $(LIBC_A)

$(BUILD)/rootfs/usr/lib/nircc/include/.installed: $(wildcard bin/nircc/include/*.h)
	$(LOG) "INST" "$(dir $@)"
	@mkdir -p $(dir $@)
	$(Q)cp bin/nircc/include/stddef.h bin/nircc/include/stdarg.h bin/nircc/include/lim.h $(dir $@)
	@touch $@

$(BUILD)/rootfs/usr/lib/crt0.o: $(CRT0)
	$(LOG) "CP" "$@"
	@mkdir -p $(dir $@)
	$(Q)cp $< $@

$(BUILD)/rootfs/usr/lib/libc.a: $(LIBC_A)
	$(LOG) "CP" "$@"
	@mkdir -p $(dir $@)
	$(Q)cp $< $@

$(BUILD)/rootfs/usr/share/examples/hello/hello.o: rootfs/usr/share/examples/hello/hello.c $(AUTOCONF)
	$(LOG) "CC" "$@"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(UCFLAGS) -c $< -o $@

$(BUILD)/rootfs/usr/include/.installed: $(SYSHDRS)
	$(LOG) "INST" "$(dir $@)"
	@mkdir -p $(dir $@)/sys
	$(Q)cp lib/libc/include/*.h $(dir $@)
	$(Q)cp lib/libc/include/sys/*.h include/sys/*.h $(dir $@)sys/
	@touch $@

$(BUILD)/rootfs/usr/share/doc/LICENSE: LICENSE
	$(LOG) "CP" "$@"
	@mkdir -p $(dir $@)
	$(Q)cp $< $@

$(BUILD)/rootfs/%: rootfs/%
	$(LOG) "CP" "$@"
	@mkdir -p $(dir $@)
	$(Q)cp $< $@

LIMINE_EFI := $(BUILD)/rootfs/usr/share/limine/BOOTX64.EFI

$(LIMINE_EFI): | limine
	$(LOG) "CP" "$@"
	@mkdir -p $(dir $@)
	$(Q)cp $(LIMINE)/BOOTX64.EFI $@

$(INITRD): $(ROOTBINS) $(ROOTETC) $(LIMINE_EFI)
	$(LOG) "TAR" "$@"
	@for a in $(ALIASES); do cp -f "$(BUILD)/rootfs/$${a#*:}" "$(BUILD)/rootfs/$${a%%:*}"; done
	@chmod 600 "$(BUILD)/rootfs/etc/passwd" 2>/dev/null || true
	$(Q)tar --format=ustar --sort=name --owner=0 --group=0 --numeric-owner \
		-cf $@ -C $(BUILD)/rootfs $(sort $(foreach f,$(PROGS) $(MPROGS) $(patsubst rootfs/%,%,$(shell find rootfs -type f)),$(firstword $(subst /, ,$(f)))))

limine:
	$(LOG) "LIMINE" "$(LIMINE)"
	@test -d $(LIMINE) || git clone --depth=1 --branch=$(LIMINE_BRANCH) \
		https://github.com $(LIMINE)
	$(Q)$(MAKE) -C $(LIMINE)

iso: $(ISO)

$(ISO): $(KERNEL) $(INITRD) boot/limine.conf $(AUTOCONF) | limine
	$(LOG) "ISO" "$@"
	$(Q)rm -rf $(BUILD)/iso
	$(Q)mkdir -p $(BUILD)/iso/boot/limine $(BUILD)/iso/EFI/BOOT
	$(Q)cp $(KERNEL) $(BUILD)/iso/boot/nirmala
	$(Q)cp $(INITRD) $(BUILD)/iso/boot/initrd.tar
	$(Q)sed -e 's/^timeout:.*/timeout: $(BOOT_TIMEOUT)/' \
	        -e 's|@CMDLINE@|$(CMDLINE)|' \
	        boot/limine.conf > $(BUILD)/iso/boot/limine/limine.conf
	$(Q)cp $(LIMINE)/limine-bios.sys $(LIMINE)/limine-bios-cd.bin \
	   $(LIMINE)/limine-uefi-cd.bin $(BUILD)/iso/boot/limine/
	$(Q)cp $(LIMINE)/BOOTX64.EFI $(BUILD)/iso/EFI/BOOT/
	$(Q)xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(BUILD)/iso -o $@
	$(Q)$(LIMINE)/limine bios-install $@

$(DISK):
	$(LOG) "DISK" "$@"
	$(Q)truncate -s $(DISKSIZE) $@

disk-reset:
	$(LOG) "RM" "$(DISK)"
	$(Q)rm -f $(DISK)

run: $(ISO) $(DISKDEP)
	$(LOG) "QEMU" "$(ISO)"
	$(Q)$(QEMU) $(QEMUFLAGS)

run-gui: $(ISO) $(DISKDEP)
	$(LOG) "QEMU" "$(ISO)"
	$(Q)$(QEMU) -M q35 -m $(QEMU_MEM) -cdrom $(ISO) $(QEMUCON) $(QEMUDISK) $(QEMUNET) -no-reboot -no-shutdown

debug: $(ISO) $(DISKDEP)
	$(LOG) "DEBUG" "$(ISO)"
	$(Q)$(QEMU) $(QEMUFLAGS) -s -S -d int,cpu_reset

clean:
	$(LOG) "CLEAN" "$(BUILD)"
	$(Q)rm -rf $(BUILD)

distclean: clean
	$(LOG) "CLEAN" "$(LIMINE) .config"
	$(Q)rm -rf $(LIMINE) .config

HOSTCC    ?= gcc
HOSTFLAGS := -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-array-bounds -Wno-unused-parameter -Iinclude -Isys/include -Isys/arch/amd64/include

test: all
	$(LOG) "TEST" "mkroot_test"
	$(Q)$(HOSTCC) $(HOSTFLAGS) -idirafter lib/libc/include -DLEMON_HOST_TEST tests/mkroot_test.c bin/install/src/mkroot.c \
		sys/fs/lemon_core.c lib/libdisk/gpt.c -o $(BUILD)/mkroot_test
	$(Q)$(BUILD)/mkroot_test
	$(LOG) "TEST" "lemon_test"
	$(Q)$(HOSTCC) $(HOSTFLAGS) -DLEMON_HOST_TEST tests/lemon_test.c sys/fs/lemon_core.c -o $(BUILD)/lemon_test
	$(Q)$(BUILD)/lemon_test
	$(LOG) "TEST" "vfs_test"
	$(Q)$(HOSTCC) $(HOSTFLAGS) -DLEMON_HOST_TEST=0 tests/vfs_test.c sys/fs/vfs.c sys/fs/lemonfs.c sys/fs/lemon_core.c \
