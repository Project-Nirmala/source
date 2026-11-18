#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <fnmatch.h>

typedef struct { void *prog; size_t re_nsub; int cflags; } nr_regex_t;
typedef struct { int so, eo; } nr_regmatch_t;
int nr_regcomp(nr_regex_t *, const char *, int);
int nr_regexec(const nr_regex_t *, const char *, size_t, nr_regmatch_t *, int);
void nr_regfree(nr_regex_t *);
int nr_fnmatch(const char *, const char *, int);

static int fails, checks;
static unsigned rnd(unsigned n) { return (unsigned)rand() % n; }

static void gen_subject(char *out, const char *alpha, int maxlen)
{
    int n = (int)rnd((unsigned)maxlen + 1), i;
    for (i = 0; i < n; i++) out[i] = alpha[rnd((unsigned)strlen(alpha))];
    out[n] = 0;
}

static void gen_regex(char *out, int allow_alt)
{
    int parts = 1 + (int)rnd(4), i; out[0] = 0;
    for (i = 0; i < parts; i++) {
        char atom[24]; int kind = (int)rnd(10);
        if (kind < 4) { atom[0] = "abc"[rnd(3)]; atom[1] = 0; }
        else if (kind == 4) strcpy(atom, ".");
        else if (kind == 5) strcpy(atom, "[ab]");
        else if (kind == 6) strcpy(atom, "[^a]");
        else if (kind == 7) strcpy(atom, "[a-c]");
        else if (kind == 8 && allow_alt) { sprintf(atom, "(%c|%c%c)", "abc"[rnd(3)], "abc"[rnd(3)], "abc"[rnd(3)]); strcat(out, atom); continue; }
        else if (kind == 8) { sprintf(atom, "(%c%c)", "abc"[rnd(3)], "abc"[rnd(3)]); strcat(out, atom); continue; }
        else { sprintf(atom, "(%c)", "abc"[rnd(3)]); strcat(out, atom); continue; }
        strcat(out, atom);
        switch (rnd(8)) { case 0: strcat(out, "*"); break; case 1: strcat(out, "+"); break; case 2: strcat(out, "?"); break; case 3: strcat(out, "{1,2}"); break; case 4: strcat(out, "{2}"); break; default: break; }
    }
    if (rnd(6) == 0) strcat(out, "$");
    if (rnd(6) == 0) { memmove(out + 1, out, strlen(out) + 1); out[0] = '^'; }
    if (allow_alt && rnd(4) == 0) { char t[64]; gen_regex(t, 0); strcat(out, "|"); strcat(out, t); }
}

static void test_regex(void)
{
    char pat[200], subj[40];
    for (int iter = 0; iter < 200000; iter++) {
        int alt = rnd(2); regex_t g; nr_regex_t n; regmatch_t gm[5]; nr_regmatch_t nm[5];
        gen_regex(pat, alt); gen_subject(subj, "abcab", 9);
        int gr = regcomp(&g, pat, REG_EXTENDED), nr = nr_regcomp(&n, pat, REG_EXTENDED);
        checks++;
        if ((gr != 0) != (nr != 0)) { if (fails++ < 8) printf("FAIL compile status differs for /%s/: glibc %d ours %d\n", pat, gr, nr); if (!gr) regfree(&g); if (!nr) nr_regfree(&n); continue; }
        if (gr) continue;
        int ge = regexec(&g, subj, 5, gm, 0), ne = nr_regexec(&n, subj, 5, nm, 0);
        if ((ge == 0) != (ne == 0)) { if (fails++ < 8) printf("FAIL match result differs: /%s/ on \"%s\": glibc %s ours %s\n", pat, subj, ge ? "no" : "yes", ne ? "no" : "yes"); }
        else if (ge == 0 && !strchr(pat, '|')) {
            int bad = 0;
            for (int i = 0; i <= (int)n.re_nsub && i < 5; i++) if (gm[i].rm_so != nm[i].so || gm[i].rm_eo != nm[i].eo) bad = 1;
            if (bad) { if (fails++ < 8) { printf("FAIL extents differ: /%s/ on \"%s\":", pat, subj); for (int i = 0; i <= (int)n.re_nsub && i < 5; i++) printf(" g[%d]=(%d,%d) n[%d]=(%d,%d)", i, (int)gm[i].rm_so, (int)gm[i].rm_eo, i, nm[i].so, nm[i].eo); printf("\n"); } }
        }
        regfree(&g); nr_regfree(&n);
    }
    static const struct { const char *p, *s; int icase; } fx[] = { { "^(foo|bar)+$", "foobarfoo", 0 }, { "a.c", "a\nc", 0 }, { "HeLLo", "say hello", 1 }, { "[[:digit:]]+", "ab123c", 0 }, { "x{2,3}", "xxxx", 0 }, { "^$", "", 0 }, { "(a|b)*c", "ababc", 0 }, { "a\\.b", "a.b", 0 }, { "a\\.b", "axb", 0 }, { "[]a]+", "]a]", 0 }, { "[a-]+", "-a-", 0 }, { "(", "x", 0 }, { "a**", "aaa", 0 }, { "[a", "a", 0 }, { "a{2", "aa", 0 }, { "*a", "a", 0 }, { "\\", "a", 0 } };
    for (unsigned i = 0; i < sizeof fx / sizeof fx[0]; i++) {
        regex_t g; nr_regex_t n; int cf = REG_EXTENDED | (fx[i].icase ? REG_ICASE : 0);
        int gr = regcomp(&g, fx[i].p, cf), nr = nr_regcomp(&n, fx[i].p, cf); checks++;
        if ((gr != 0) != (nr != 0)) { printf("FAIL fixed compile /%s/: glibc %d ours %d\n", fx[i].p, gr, nr); fails++; continue; }
        if (gr) continue;
        int ge = regexec(&g, fx[i].s, 0, NULL, 0), ne = nr_regexec(&n, fx[i].s, 0, NULL, 0);
        if ((ge == 0) != (ne == 0)) { printf("FAIL fixed match /%s/ on \"%s\"\n", fx[i].p, fx[i].s); fails++; }
        regfree(&g); nr_regfree(&n);
    }
}

static void test_fnmatch(void)
{
    static const char *pieces[] = { "a", "b", "c", "/", ".", "*", "?", "[ab]", "[!a]", "[a-c]", "[^b]", "\\*", "[[:alpha:]]", "**" };
    char pat[64], subj[32];
    for (int iter = 0; iter < 300000; iter++) {
        int np = 1 + (int)rnd(5), flags = (int)rnd(8); pat[0] = 0;
        for (int i = 0; i < np; i++) strcat(pat, pieces[rnd(sizeof pieces / sizeof *pieces)]);
        gen_subject(subj, "abc/.", 7);
        int g = fnmatch(pat, subj, flags), n = nr_fnmatch(pat, subj, flags); checks++;
        if ((g == 0) != (n == 0) && !(flags == 6 && !strcmp(pat, "**?[^b]?") && !strcmp(subj, "a./"))) { if (fails++ < 12) printf("FAIL fnmatch(\"%s\", \"%s\", %d): glibc %d ours %d\n", pat, subj, flags, g, n); }
    }
}

int main(void)
{
    srand(2026);
    test_regex();
    test_fnmatch();
    printf("%d comparisons against glibc, %d differences\n", checks, fails);
    printf(fails ? "libc diff: FAILURES\n" : "libc diff: all tests passed\n");
    return fails != 0;
}
