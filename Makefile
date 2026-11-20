GOALS = all iso run run-gui debug limine test disk-reset clean distclean \
        menuconfig defconfig showconfig setconfig
LIMINE = limine
ENVV = V="$(V)" SELFTEST="$(SELFTEST)" TRACE="$(TRACE)" NODISK="$(NODISK)" \
       HOSTCC="$(HOSTCC)" NCLIBS="$(NCLIBS)" LIMINE="$(LIMINE)" MAKE_VERSION="$(MAKE_VERSION)"

$(GOALS):
	@$(ENVV) sh scripts/dispatch.sh "$(MAKE)" $@

.DEFAULT:
	@$(ENVV) sh scripts/dispatch.sh "$(MAKE)" $@

.PHONY: $(GOALS)
