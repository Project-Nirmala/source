include build/config.mk
include build/rules.mk

.SUFFIXES:

ALIASES = bin/make:bin/nirmake bin/ld:bin/nirld bin/[:bin/test bin/poweroff:bin/reboot bin/halt:bin/reboot
ROOTETC = $(ROOTCOPY) build/rootfs/usr/share/doc/LICENSE \
          build/rootfs/usr/lib/crt0.o build/rootfs/usr/lib/libc.a \
          build/rootfs/usr/share/examples/hello/hello.o build/rootfs/usr/include/.installed \
          build/rootfs/usr/lib/nircc/include/.installed

all: $(KERNEL) $(INITRD)

$(AUTOCONF): .config
	$(LOG) "GEN" "$@"
	@mkdir -p build/
	$(Q)$(NIRCONFIG) -k Kconfig -c .config -H $@ gen

menuconfig:
	$(Q)$(NIRCONFIG) -k Kconfig -c .config menu

defconfig:
	$(LOG) "CONF" ".config"
	$(Q)$(NIRCONFIG) -k Kconfig -c .config default

showconfig:
	@$(NIRCONFIG) -k Kconfig -c .config list

setconfig:
	$(Q)$(NIRCONFIG) -k Kconfig -c .config set $(SET)

$(KERNEL): $(OBJS)
	$(LOG) "LD" "$@"
	$(Q)$(LD) $(LDFLAGS) -o $@ $(OBJS)

build/rootfs/usr/lib/nircc/include/.installed: $(NIRCCHDRS)
	$(LOG) "INST" "build/rootfs/usr/lib/nircc/include/"
	@mkdir -p build/rootfs/usr/lib/nircc/include/
	$(Q)cp bin/nircc/include/stddef.h bin/nircc/include/stdarg.h bin/nircc/include/lim.h build/rootfs/usr/lib/nircc/include/
	@touch $@

build/rootfs/usr/lib/crt0.o: $(CRT0)
	$(LOG) "CP" "$@"
	@mkdir -p build/rootfs/usr/lib/
	$(Q)cp $(CRT0) $@

build/rootfs/usr/lib/libc.a: $(LIBC_A)
	$(LOG) "CP" "$@"
	@mkdir -p build/rootfs/usr/lib/
	$(Q)cp $(LIBC_A) $@

build/rootfs/usr/share/examples/hello/hello.o: rootfs/usr/share/examples/hello/hello.c $(AUTOCONF)
	$(LOG) "CC" "$@"
	@mkdir -p build/rootfs/usr/share/examples/hello/
	$(Q)$(CC) $(UCFLAGS) -c rootfs/usr/share/examples/hello/hello.c -o $@

build/rootfs/usr/include/.installed: $(SYSHDRS)
	$(LOG) "INST" "build/rootfs/usr/include/"
	@mkdir -p build/rootfs/usr/include/sys
	$(Q)cp lib/libc/include/*.h build/rootfs/usr/include/
	$(Q)cp lib/libc/include/sys/*.h include/sys/*.h build/rootfs/usr/include/sys/
	@touch $@

build/rootfs/usr/share/doc/LICENSE: LICENSE
	$(LOG) "CP" "$@"
	@mkdir -p build/rootfs/usr/share/doc/
	$(Q)cp LICENSE $@

$(INITRD): $(ROOTBINS) $(ROOTETC)
	$(LOG) "TAR" "$@"
	@for a in $(ALIASES); do cp -f "$(BUILD)/rootfs/$${a#*:}" "$(BUILD)/rootfs/$${a%%:*}"; done
	@chmod 600 "$(BUILD)/rootfs/etc/passwd" 2>/dev/null || true
	$(Q)tar --format=ustar --sort=name --owner=0 --group=0 --numeric-owner \
		-cf $@ -C $(BUILD)/rootfs $(TARDIRS)

$(LIMINE)/limine:
	$(LOG) "LIMINE" "$(LIMINE)"
	@test -d $(LIMINE) || git clone --depth=1 --branch=$(LIMINE_BRANCH) \
		https://github.com $(LIMINE)
	$(Q)cd $(LIMINE) && $(MAKE)

limine: $(LIMINE)/limine

iso: $(ISO)

$(ISO): $(KERNEL) $(INITRD) boot/limine.conf $(AUTOCONF) $(LIMINE)/limine
	$(LOG) "ISO" "$@"
	$(Q)rm -rf $(BUILD)/iso
	$(Q)mkdir -p $(BUILD)/iso/boot/limine $(BUILD)/iso/EFI/BOOT
	$(Q)cp $(KERNEL) $(BUILD)/iso/boot/nirmala
	$(Q)cp $(INITRD) $(BUILD)/iso/boot/initrd.tar
	$(Q)sed -e 's/^timeout:.*/timeout: $(BOOT_TIMEOUT)/' \
		-e 's|^\([[:space:]]*cmdline:.*\)|\1 cols=$(COLS) rows=$(ROWS)|' \
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
	$(Q)$(QEMU) -M q35 -m $(QEMU_MEM) -cdrom $(ISO) $(QEMUCON) $(QEMUDISK) -no-reboot -no-shutdown

debug: $(ISO) $(DISKDEP)
	$(LOG) "DEBUG" "$(ISO)"
	$(Q)$(QEMU) $(QEMUFLAGS) -s -S -d int,cpu_reset

test: all
	$(LOG) "TEST" "lemon_test"
	$(Q)$(HOSTCC) $(HOSTFLAGS) -DLEMON_HOST_TEST tests/lemon_test.c sys/fs/lemon_core.c -o $(BUILD)/lemon_test
	$(Q)$(BUILD)/lemon_test

.PHONY: all iso run run-gui debug limine test disk-reset menuconfig defconfig showconfig setconfig
