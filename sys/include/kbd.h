#ifndef _NIRMALA_KBD_H
#define _NIRMALA_KBD_H

/* Select a keyboard layout by name ("us", "dvorak", "colemak"). 0 or -ENOENT. */
int kbd_set_map(const char *name);

#endif
