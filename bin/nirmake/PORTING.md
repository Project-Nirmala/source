# nirmake porting notes

## Scope

nirmake is a POSIX make written in C89 (`-std=c89 -pedantic`, `_POSIX_C_SOURCE=200809L`). It reads
bmake-style makefiles. Behavior was compared against bmake 20200710 with a differential harness
(about 28 makefile cases: modifiers, conditionals, `.for`, suffix chains, specials, messages) and
the `tests/run.sh` suite.

## Source layout

- `util.c` memory, buffers, string lists, word splitting, messages
- `var.c` variable table, expansion, modifiers
- `cond.c` `.if` expression evaluator
- `parse.c` line reader, directives, assignments, rules, `.for`, `.include`
- `node.c` target graph, suffix and `%` rule inference, timestamps
- `builtin.c` default variables and suffix rules
- `make.c` scheduler (serial and `-j`), out-of-date logic, command execution
- `job.c` fork/exec, wait, signal cleanup
- `main.c` options, MAKEFLAGS, startup

## C89 and POSIX decisions

- No `//` comments, no mixed declarations, no `long long`, no `inline`, no `va_copy`.
- Message formatting uses `vsprintf` into a 4096-byte buffer; no `snprintf` (not C89).
- Process control uses `fork`, `execl("/bin/sh", "sh", "-ec", ...)`, `waitpid`.
- Regular expressions (`:C`) use POSIX `regcomp` with `REG_EXTENDED`.
- Timestamps use `st_mtim.tv_nsec` (`st_mtimespec` on macOS); define `NM_NO_NSEC` to fall back to seconds.

## bmake behavior that is matched on purpose

- Default makefile search is `makefile`, then `Makefile`. `BSDmakefile` is not read.
- `*** Error code N`, `Stop.`, `stopped in DIR` and `not remade because of errors` go to stdout;
  `don't know how to make X. Stop` goes to stderr and exits 2. Other failures exit 1.
- `.error` exits 1. `.info` and `.warning` print `nirmake: "file" line N: ...` to stderr.
- Continuation lines keep the whitespace before the backslash and add one space.
- `:H` keeps empty words and yields `.` for words without a slash; `:E` drops empty words.
- `:Q` quotes the whole value. `:[*]` returns the value unchanged. `:ts:` uses `:` as separator.
- Cycles print `Graph cycles through` and mark the target as failed.
- With `-n`, every command is printed, including `@` lines, and `$(MAKE)` lines still run.

## Extensions over bmake

- `%` pattern rules (`%.o: %.c`), with zero or more sources.
- `$<` and `$^` are set for explicit rules as well (bmake leaves them empty there).
- `.export VAR` may precede the assignment; the value is exported at job start.
- `-j N` is a real parallel scheduler; `-j1` keeps depth-first post-order.

## Known differences and gaps

- `-q` exits 0 when everything is up to date (bmake 20200710 exits 1 in the same case).
- `-k` with failures exits 1 (bmake 20200710 exited 0 in the cases tested).
- Messages show the makefile path as given; bmake shows an absolute path.
- Output of parallel jobs is not buffered per job.
- Not implemented: GNU functions (`$(wildcard)`, `$(patsubst)`, `$(shell)`), `.ORDER`, `.WAIT`,
  `.USE`/`.EXEC` semantics (parsed, ignored), `.PATH`, `.SHELL`, meta mode, `:range`, `:hash`,
  `:tA`, `:?:` conditionals, target-specific variables, `sys.mk` loading, `.OBJDIR` switching.
- `-d`, `-m`, `-p`, `-w` are accepted and ignored.

## Real-world check

`examples/nircc.mk` is a bmake-compatible rewrite of the GNU-only Makefile of
`project-nirmala/nircc`. With `nirmake -f nircc.mk -j4 all self` it builds `nircc` and rebuilds it
with its own driver; results match GNU make and bmake on the same checkout.
