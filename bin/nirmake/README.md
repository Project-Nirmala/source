# nirmake

Nirmala make: a POSIX make written in C89, compatible with bmake makefiles.

    make                      # first build with any make
    ./nirmake                 # rebuild with itself
    ./nirmake test            # run tests/run.sh
    ./nirmake -j4 -f examples/nircc.mk all

Options: -BeiknqrSst -C dir -D var -f file -I dir -j jobs -V var, plus VAR=value and targets.

Layout:

    src/        sources and the single shared header
    tests/      run.sh test suite
    examples/   sample makefiles
    PORTING.md  design notes, bmake compatibility, known gaps
