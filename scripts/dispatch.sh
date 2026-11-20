#!/bin/sh
set -e

MAKE_CMD=$1
GOAL=$2
BUILD=build
NIRCONFIG=$BUILD/host/nirconfig
V=${V:-}
HOSTCC=${HOSTCC:-gcc}
NCLIBS=${NCLIBS:--lcurses}
LIMINE=${LIMINE:-limine}
export V SELFTEST TRACE NODISK HOSTCC LIMINE

step() {
	tag=$1
	label=$2
	shift 2
	if [ "$V" = 1 ]; then
		echo "$*"
	else
		printf '  %-5s %s\n' "$tag" "$label"
	fi
	"$@"
}

host_stale() {
	[ -x "$NIRCONFIG" ] || return 0
	[ -n "$(find tools/nirconfig -type f -newer "$NIRCONFIG" 2>/dev/null | head -n 1)" ]
}

config_stale() {
	[ -f .config ] || return 0
	[ -n "$(find Kconfig -newer .config 2>/dev/null | head -n 1)" ]
}

case $GOAL in
clean)
	step CLEAN "$BUILD" rm -rf "$BUILD"
	exit 0
	;;
distclean)
	step CLEAN "$BUILD" rm -rf "$BUILD"
	step CLEAN "$LIMINE .config" rm -rf "$LIMINE" .config
	exit 0
	;;
esac

case $GOAL in
limine|disk-reset)
	need_host=0
	;;
*)
	need_host=1
	;;
esac

if [ "$need_host" = 1 ]; then
	if host_stale; then
		mkdir -p "$BUILD/host"
		step HOST "$NIRCONFIG" $HOSTCC -std=c89 -pedantic -O1 -Wall -Wextra -o "$NIRCONFIG" tools/nirconfig/*.c $NCLIBS
	fi
	if [ "$GOAL" != defconfig ] && config_stale; then
		step CONF .config "$NIRCONFIG" -k Kconfig -c .config old
	fi
fi

sh scripts/genconf.sh
sh scripts/genrules.sh

case $MAKE_VERSION in
?*)
	exec "$MAKE_CMD" --no-print-directory -f mk/main.mk "$GOAL"
	;;
esac
exec "$MAKE_CMD" -f mk/main.mk "$GOAL"
