#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#define T_NUM 0
#define T_STR 1
#define T_NIL 2
#define T_BOOL 3

typedef struct cval {
    int t;
    union { double n; const char *s; } v;
} cval;

#define CVSPOOL 1024
#define CVSSZ 4096
static char ccpl_strs[CVSPOOL][CVSSZ];
static int ccpl_str_i = 0;

static char *cvsbuf(void) {
    char *p = ccpl_strs[ccpl_str_i];
    ccpl_str_i = (ccpl_str_i + 1) % CVSPOOL;
    return p;
}

static cval cvnum(double n) { cval r; r.t = T_NUM; r.v.n = n; return r; }
static cval cvstr(const char *s) { cval r; r.t = T_STR; r.v.s = s; return r; }
static cval cvnil(void) { cval r; r.t = T_NIL; r.v.n = 0; return r; }
static cval cvbool(double b) { cval r; r.t = T_BOOL; r.v.n = b ? 1 : 0; return r; }

static void ccpl_die(const char *msg) {
    fprintf(stderr, "fatal: %s\n", msg);
    exit(1);
}

static int cvtruthy(cval x) {
    if (x.t == T_NIL) return 0;
    if (x.t == T_BOOL) return x.v.n != 0;
    return 1;
}

static const char *cvs(cval x) {
    char *d;
    if (x.t == T_STR) return x.v.s;
    d = cvsbuf();
    if (x.t == T_NUM) {
        double v = x.v.n;
        if (isfinite(v) && v == floor(v) && fabs(v) <= 9007199254740992.0)
            snprintf(d, CVSSZ, "%lld", (long long)v);
        else
            snprintf(d, CVSSZ, "%.15g", v);
    }
    else if (x.t == T_BOOL) snprintf(d, CVSSZ, "%s", x.v.n ? "true" : "false");
    else snprintf(d, CVSSZ, "nil");
    return d;
}

static cval cv_error(cval m) {
    ccpl_die(cvs(m));
    return cvnil();
}

static void ccpl_print(int n, cval *args) {
    int i;
    for (i = 0; i < n; i++) {
        const char *s;
        if (i) putchar('\t');
        s = cvs(args[i]);
        fputs(s, stdout);
    }
    putchar('\n');
}

static void ccpl_warn(int n, cval *args) {
    int i;
    fprintf(stderr, "hey: ");
    for (i = 0; i < n; i++) {
        const char *s;
        if (i) fputs(" ", stderr);
        s = cvs(args[i]);
        fputs(s, stderr);
    }
    fputc('\n', stderr);
}

static double cvtod(cval x) {
    if (x.t == T_NUM) return x.v.n;
    if (x.t == T_BOOL) return x.v.n;
    if (x.t == T_STR) {
        char *end;
        double d = strtod(x.v.s, &end);
        if (end && *end == '\0') return d;
    }
    ccpl_die("expected a number");
    return 0;
}

static double ccpl_fmod(double x, double y) {
    if (y == 0) return NAN;
    return fmod(fmod(x, y) + y, y);
}

static cval cvarith(int op, cval a, cval b) {
    double x = cvtod(a), y = cvtod(b);
    switch (op) {
        case '+': return cvnum(x + y);
        case '-': return cvnum(x - y);
        case '*': return cvnum(x * y);
        case '/': return cvnum(x / y);
        case '%': return cvnum(ccpl_fmod(x, y));
    }
    ccpl_die("bad arithmetic op");
    return cvnum(0);
}

static cval cv_add(cval a, cval b) { return cvarith('+', a, b); }
static cval cv_sub(cval a, cval b) { return cvarith('-', a, b); }
static cval cv_mul(cval a, cval b) { return cvarith('*', a, b); }
static cval cv_div(cval a, cval b) { return cvarith('/', a, b); }
static cval cv_mod(cval a, cval b) { return cvarith('%', a, b); }

static cval cv_neg(cval a) { return cvnum(-cvtod(a)); }
static cval cv_not(cval a) { return cvbool(!cvtruthy(a)); }

static cval cv_eq(cval a, cval b) {
    if (a.t != b.t) return cvbool(0);
    if (a.t == T_NUM || a.t == T_BOOL) return cvbool(a.v.n == b.v.n);
    if (a.t == T_STR) return cvbool(strcmp(a.v.s, b.v.s) == 0);
    return cvbool(1);
}
static cval cv_ne(cval a, cval b) { return cv_not(cv_eq(a, b)); }

static int cvcmp(cval a, cval b) {
    if (a.t == T_STR && b.t == T_STR) return strcmp(a.v.s, b.v.s);
    {
        double x = cvtod(a), y = cvtod(b);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
}
static cval cv_lt(cval a, cval b) { return cvbool(cvcmp(a, b) < 0); }
static cval cv_le(cval a, cval b) { return cvbool(cvcmp(a, b) <= 0); }
static cval cv_gt(cval a, cval b) { return cvbool(cvcmp(a, b) > 0); }
static cval cv_ge(cval a, cval b) { return cvbool(cvcmp(a, b) >= 0); }

static cval cv_concat(cval a, cval b) {
    char *dst = cvsbuf();
    const char *xs = cvs(a);
    const char *ys = cvs(b);
    int l = snprintf(dst, CVSSZ, "%s%s", xs, ys);
    if (l < 0 || l >= CVSSZ) ccpl_die("string too long");
    return cvstr(dst);
}

static cval cv_tonumber(cval a) {
    if (a.t == T_NUM || a.t == T_BOOL) return cvnum(a.v.n);
    if (a.t == T_STR) {
        char *end;
        double d = strtod(a.v.s, &end);
        if (end && *end == '\0') return cvnum(d);
    }
    return cvnil();
}

static cval cv_tostring(cval a) {
    if (a.t == T_STR) return a;
    return cvstr(cvs(a));
}