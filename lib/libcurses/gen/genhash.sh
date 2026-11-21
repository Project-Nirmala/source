#!/bin/sh
set -e

TERMH=${1:-term.h}

genent()
{
    name=$1
    NAME=$2
    echo
    echo "static const char _ti_${name}ids[][32] = {"
    sed -e "1,/enum TI${NAME}/d" -e '/};/,$d' -e 's/.*TICODE_\([^,]*\).*/\t"\1",/' "$TERMH"
    echo "};"
    cat <<EOT

const char *
_ti_${name}id(ssize_t idx)
{
	if ((size_t)idx >= __arraycount(_ti_${name}ids))
		return NULL;
	return _ti_${name}ids[idx];
}

ssize_t
_ti_${name}index(const char *key)
{
	size_t i;

	for (i = 0; i < __arraycount(_ti_${name}ids); i++)
		if (strcmp(key, _ti_${name}ids[i]) == 0)
			return (ssize_t)i;
	return -1;
}
EOT
}

cat <<EOT
#include <sys/types.h>
#include <stddef.h>
#include <string.h>
#include <term_private.h>
EOT
genent flag FLAG
genent num NUM
genent str STR
