#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "engine.h"
#include "ui.h"

int main(int argc, char **argv)
{
    const char *target = NULL;
    int text_mode = 0, i, r;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-t"))
            text_mode = 1;
        else
            target = argv[i];
    }
    if (!inst_check_system()) {
        dprintf(2, "install: this system image cannot install itself\n");
        return 1;
    }
    if (text_mode || target || !isatty(0))
        r = ui_text_run(target);
    else {
        r = ui_curses_run();
        if (r < 0)
            r = ui_text_run(NULL);
    }
    if (getpid() == 1) {
        static char *const init_argv[] = { "init", NULL };
        char c;

        printf("Press Enter to continue to the live system.\n");
        fflush(stdout);
        while (read(0, &c, 1) == 1 && c != '\n')
            ;
        execv("/sbin/init", init_argv);
    }
    return r;
}
