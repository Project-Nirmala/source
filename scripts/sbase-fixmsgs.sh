#!/bin/sh
D=$1
sed -i 's/"rmdir %s:"/"failed to remove '"'"'%s'"'"':"/' "$D/rmdir.c"
sed -i 's/"%s -> %s:"/"cannot move '"'"'%s'"'"' to '"'"'%s'"'"':"/' "$D/mv.c" "$D/libutil/fnck.c"
sed -i 's/"utimensat %s:"/"cannot touch '"'"'%s'"'"':"/; s/"open %s:", file/"cannot touch '"'"'%s'"'"':", file/' "$D/touch.c"
sed -i 's/^extern int recurse_status;/extern int recurse_status;\nextern const char *recurse_msg;/' "$D/fs.h"
sed -i 's/^int recurse_status = 0;/int recurse_status = 0;\nconst char *recurse_msg = "cannot access";/; s/weprintf("stat %s:", r->path)/weprintf("%s '"'"'%s'"'"':", recurse_msg, r->path)/' "$D/libutil/recurse.c"
sed -i 's/^\tARGBEGIN {/\trecurse_msg = "cannot remove";\n\tARGBEGIN {/' "$D/rm.c"
sed -i -f "$(dirname "$0")/sbase-msgs.sed" "$D"/*.c "$D"/libutil/*.c
