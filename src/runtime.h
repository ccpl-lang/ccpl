/* CCPL runtime, injected into every compiled program with -include.
 *
 * Two parts live here:
 *   1. the dynamic value runtime (cval) used by the language core;
 *   2. the foreign function interface used by `extern` declarations, which
 *      can load C and C++ symbols from DLLs and call into CPython.
 *
 * Dynamic strings are immutable and are allocated for the lifetime of the
 * process. There is no garbage collector, which keeps the runtime tiny but
 * means long running programs should avoid building strings in tight loops.
 */
#ifndef CCPL_RUNTIME_H
#define CCPL_RUNTIME_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------------- */
/* small Windows shims, so the runtime does not need windows.h              */
/* ------------------------------------------------------------------------- */

typedef void *HMODULE;
typedef void *FARPROC;
extern __declspec(dllimport) HMODULE __stdcall LoadLibraryA(const char *name);
extern __declspec(dllimport) FARPROC __stdcall GetProcAddress(HMODULE mod, const char *name);
extern __declspec(dllimport) int __stdcall FreeLibrary(HMODULE mod);
extern __declspec(dllimport) unsigned long __stdcall GetEnvironmentVariableA(const char *name, char *buf, unsigned long size);
extern __declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(HMODULE mod, char *buf, unsigned long size);

/* ------------------------------------------------------------------------- */
/* dynamic values                                                           */
/* ------------------------------------------------------------------------- */

#define T_NUM 0
#define T_STR 1
#define T_NIL 2
#define T_BOOL 3

typedef struct cval {
    int t;
    union { double n; const char *s; } v;
} cval;

static void ccpl_die(const char *msg) {
    fprintf(stderr, "fatal: %s\n", msg);
    exit(1);
}

static void *ccpl_alloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) ccpl_die("out of memory");
    return p;
}

static char *ccpl_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)ccpl_alloc(n);
    memcpy(p, s, n);
    return p;
}

static cval cvnum(double n) { cval r; r.t = T_NUM; r.v.n = n; return r; }
static cval cvstr(const char *s) { cval r; r.t = T_STR; r.v.s = s; return r; }
static cval cvnil(void) { cval r; r.t = T_NIL; r.v.n = 0; return r; }
static cval cvbool(double b) { cval r; r.t = T_BOOL; r.v.n = b ? 1 : 0; return r; }

static int cvtruthy(cval x) {
    if (x.t == T_NIL) return 0;
    if (x.t == T_BOOL) return x.v.n != 0;
    return 1;
}

/* Render any value as text. Strings are returned as is, everything else is
 * formatted into a fresh allocation so the result stays valid for the life of
 * the process. */
static const char *cvs(cval x) {
    char *d = (char *)ccpl_alloc(64);
    if (x.t == T_STR) {
        free(d);
        return x.v.s;
    }
    if (x.t == T_NUM) {
        double v = x.v.n;
        if (isfinite(v) && v == floor(v) && fabs(v) <= 9007199254740992.0)
            snprintf(d, 64, "%lld", (long long)v);
        else
            snprintf(d, 64, "%.15g", v);
    } else if (x.t == T_BOOL) {
        snprintf(d, 64, "%s", x.v.n ? "true" : "false");
    } else {
        snprintf(d, 64, "nil");
    }
    return d;
}

/* Convert a dynamic value to a C string. Numbers and booleans are rendered,
 * nil becomes "nil". Useful for passing strings to foreign functions. */
static const char *cv_tocstr(cval x) { return cvs(x); }

static cval cv_error(cval m) {
    ccpl_die(cvs(m));
    return cvnil();
}

static void ccpl_print(int n, cval *args) {
    int i;
    for (i = 0; i < n; i++) {
        if (i) putchar('\t');
        fputs(cvs(args[i]), stdout);
    }
    putchar('\n');
}

static void ccpl_warn(int n, cval *args) {
    int i;
    fprintf(stderr, "hey: ");
    for (i = 0; i < n; i++) {
        if (i) fputs(" ", stderr);
        fputs(cvs(args[i]), stderr);
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
    const char *x = cvs(a);
    const char *y = cvs(b);
    size_t lx = strlen(x), ly = strlen(y);
    char *d = (char *)ccpl_alloc(lx + ly + 1);
    memcpy(d, x, lx);
    memcpy(d + lx, y, ly + 1);
    return cvstr(d);
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

/* ------------------------------------------------------------------------- */
/* C and C++ foreign functions                                              */
/* ------------------------------------------------------------------------- */

/* Resolve `sym` in `lib`, loading and caching the library on first use. The
 * generated wrapper caches the function pointer itself, so the lookup runs at
 * most once per declared function. */
static void *ccpl_ffi_sym(const char *lib, const char *sym) {
    static struct { char *lib; HMODULE mod; } cache[128];
    static int n = 0;
    HMODULE mod = 0;
    int i;
    for (i = 0; i < n; i++) {
        if (!strcmp(cache[i].lib, lib)) { mod = cache[i].mod; break; }
    }
    if (!mod) {
        mod = LoadLibraryA(lib);
        if (!mod) {
            fprintf(stderr, "fatal: cannot load library '%s'\n", lib);
            exit(1);
        }
        if (n < (int)(sizeof cache / sizeof cache[0])) {
            cache[n].lib = ccpl_strdup(lib);
            cache[n].mod = mod;
            n++;
        }
    }
    {
        FARPROC p = GetProcAddress(mod, sym);
        if (!p) {
            fprintf(stderr, "fatal: symbol '%s' not found in '%s'\n", sym, lib);
            exit(1);
        }
        return (void *)p;
    }
}

/* ------------------------------------------------------------------------- */
/* Python foreign functions                                                 */
/* ------------------------------------------------------------------------- */

typedef struct PyObjectStub PyObject;
typedef struct { long ob_refcnt; void *ob_type; } CCPL_PyHdr;

/* Type objects and singletons live inside the Python DLL, so they are looked
 * up with GetProcAddress at run time instead of being linked. */

#define CCPL_PY_EVAL_INPUT 258
#define CCPL_PY_FILE_INPUT 257

typedef void (*ccpl_py_init_t)(void);
typedef int (*ccpl_py_isinit_t)(void);
typedef PyObject *(*ccpl_py_import_t)(const char *);
typedef PyObject *(*ccpl_py_getattr_t)(PyObject *, const char *);
typedef PyObject *(*ccpl_py_call_t)(PyObject *, PyObject *, PyObject *);
typedef PyObject *(*ccpl_py_tuple_new_t)(long);
typedef int (*ccpl_py_tuple_set_t)(PyObject *, long, PyObject *);
typedef PyObject *(*ccpl_py_long_t)(long long);
typedef PyObject *(*ccpl_py_float_t)(double);
typedef PyObject *(*ccpl_py_unicode_t)(const char *);
typedef const char *(*ccpl_py_utf8_t)(PyObject *);
typedef long long (*ccpl_py_aslong_t)(PyObject *);
typedef double (*ccpl_py_asdouble_t)(PyObject *);
typedef double (*ccpl_py_asfloat_t)(PyObject *);
typedef int (*ccpl_py_istrue_t)(PyObject *);
typedef PyObject *(*ccpl_py_err_occ_t)(void);
typedef void (*ccpl_py_err_print_t)(void);
typedef void (*ccpl_py_decref_t)(PyObject *);
typedef void (*ccpl_py_incref_t)(PyObject *);
typedef PyObject *(*ccpl_py_run_t)(const char *, int, PyObject *, PyObject *);
typedef PyObject *(*ccpl_py_addmod_t)(const char *);
typedef PyObject *(*ccpl_py_moddict_t)(PyObject *);

static HMODULE ccpl_py_lib = 0;
static int ccpl_py_tried = 0;

static HMODULE ccpl_py_module(void) {
    if (ccpl_py_lib) return ccpl_py_lib;
    if (ccpl_py_tried) return 0;
    ccpl_py_tried = 1;

    {
        char buf[512];
        unsigned long n = GetEnvironmentVariableA("CCPL_PYTHON_DLL", buf, (unsigned long)sizeof buf);
        if (n > 0 && n < sizeof buf) ccpl_py_lib = LoadLibraryA(buf);
    }
    /* Prefer a versioned DLL: python3.dll is the stable ABI and does not export
     * everything this runtime needs (for example PyRun_String). */
    if (!ccpl_py_lib) {
        const char *roots[3];
        char buf[512];
        int v;
        roots[0] = getenv("LOCALAPPDATA");
        roots[1] = getenv("ProgramFiles");
        roots[2] = getenv("ProgramFiles(x86)");
        for (v = 20; v >= 8 && !ccpl_py_lib; v--) {
            int r;
            for (r = 0; r < 3 && !ccpl_py_lib; r++) {
                if (!roots[r] || !*roots[r]) continue;
                snprintf(buf, sizeof buf, "%s\\Programs\\Python\\Python3%d\\python3%d.dll", roots[r], v, v);
                ccpl_py_lib = LoadLibraryA(buf);
                if (!ccpl_py_lib) {
                    snprintf(buf, sizeof buf, "%s\\Python3%d\\python3%d.dll", roots[r], v, v);
                    ccpl_py_lib = LoadLibraryA(buf);
                }
            }
        }
    }
    if (!ccpl_py_lib) ccpl_py_lib = LoadLibraryA("python3.dll");
    if (ccpl_py_lib && !GetProcAddress(ccpl_py_lib, "PyRun_String")) {
        /* The stable ABI DLL does not expose the full API. Read the version
         * from the DLL, then load the matching versioned DLL next to it. */
        char path[1024];
        if (GetModuleFileNameA(ccpl_py_lib, path, (unsigned long)sizeof path)) {
            typedef const char *(*getver_t)(void);
            getver_t gv = (getver_t)GetProcAddress(ccpl_py_lib, "Py_GetVersion");
            int major = 3, minor = 0;
            if (gv) {
                const char *v = gv();
                if (v && v[0] && v[1] == '.') {
                    major = v[0] - '0';
                    minor = atoi(v + 2);
                }
            }
            {
                char *slash = strrchr(path, '\\');
                if (slash) {
                    HMODULE full;
                    snprintf(slash + 1, (size_t)(path + sizeof path - (slash + 1)),
                             "python%d%d.dll", major, minor);
                    full = LoadLibraryA(path);
                    if (full) {
                        FreeLibrary(ccpl_py_lib);
                        ccpl_py_lib = full;
                    }
                }
            }
        }
    }
    return ccpl_py_lib;
}

static void *ccpl_py_sym(const char *name) {
    HMODULE m = ccpl_py_module();
    FARPROC p;
    if (!m) {
        ccpl_die("cannot load a Python runtime; set CCPL_PYTHON_DLL to python3.dll");
        return 0;
    }
    p = GetProcAddress(m, name);
    if (!p) {
        fprintf(stderr, "fatal: Python runtime is missing '%s'\n", name);
        exit(1);
    }
    return (void *)p;
}

static void ccpl_py_fail(const char *where) {
    fputs("python: ", stderr);
    fputs(where, stderr);
    fputc('\n', stderr);
    ((ccpl_py_err_print_t)ccpl_py_sym("PyErr_Print"))();
    exit(1);
}

static void ccpl_py_init(void) {
    if (((ccpl_py_isinit_t)ccpl_py_sym("Py_IsInitialized"))()) return;
    ((ccpl_py_init_t)ccpl_py_sym("Py_Initialize"))();
}

static PyObject *ccpl_py_from(cval x) {
    if (x.t == T_STR) {
        PyObject *o = ((ccpl_py_unicode_t)ccpl_py_sym("PyUnicode_FromString"))(x.v.s);
        if (!o) ccpl_py_fail("cannot convert string");
        return o;
    }
    if (x.t == T_NIL) {
        PyObject *o = (PyObject *)ccpl_py_sym("_Py_NoneStruct");
        ((ccpl_py_incref_t)ccpl_py_sym("Py_IncRef"))(o);
        return o;
    }
    if (x.t == T_BOOL || (x.t == T_NUM && x.v.n == floor(x.v.n) &&
                          x.v.n >= -9007199254740992.0 && x.v.n <= 9007199254740992.0)) {
        PyObject *o = ((ccpl_py_long_t)ccpl_py_sym("PyLong_FromLongLong"))((long long)x.v.n);
        if (!o) ccpl_py_fail("cannot convert integer");
        return o;
    }
    {
        PyObject *o = ((ccpl_py_float_t)ccpl_py_sym("PyFloat_FromDouble"))(x.v.n);
        if (!o) ccpl_py_fail("cannot convert number");
        return o;
    }
}

static cval ccpl_py_to(PyObject *o) {
    void *ty;
    if (!o) return cvnil();
    if (o == (PyObject *)ccpl_py_sym("_Py_NoneStruct")) return cvnil();
    ty = ((CCPL_PyHdr *)o)->ob_type;
    if (ty == ccpl_py_sym("PyBool_Type"))
        return cvbool(((ccpl_py_istrue_t)ccpl_py_sym("PyObject_IsTrue"))(o));
    if (ty == ccpl_py_sym("PyLong_Type"))
        return cvnum(((ccpl_py_asdouble_t)ccpl_py_sym("PyLong_AsDouble"))(o));
    if (ty == ccpl_py_sym("PyFloat_Type"))
        return cvnum(((ccpl_py_asfloat_t)ccpl_py_sym("PyFloat_AsDouble"))(o));
    if (ty == ccpl_py_sym("PyUnicode_Type")) {
        const char *s = ((ccpl_py_utf8_t)ccpl_py_sym("PyUnicode_AsUTF8"))(o);
        return cvstr(ccpl_strdup(s ? s : ""));
    }
    return cvstr("<python object>");
}

/* Call module.attribute(args...). Used by generated wrappers for Python. */
static cval ccpl_py_call(const char *mod, const char *attr, int n, cval *args) {
    PyObject *m, *f, *t, *r;
    cval v;
    int i;
    ccpl_py_init();
    m = ((ccpl_py_import_t)ccpl_py_sym("PyImport_ImportModule"))(mod);
    if (!m) ccpl_py_fail("import failed");
    f = ((ccpl_py_getattr_t)ccpl_py_sym("PyObject_GetAttrString"))(m, attr);
    ((ccpl_py_decref_t)ccpl_py_sym("Py_DecRef"))(m);
    if (!f) ccpl_py_fail("attribute lookup failed");
    t = ((ccpl_py_tuple_new_t)ccpl_py_sym("PyTuple_New"))(n);
    if (!t) ccpl_py_fail("cannot build arguments");
    for (i = 0; i < n; i++)
        ((ccpl_py_tuple_set_t)ccpl_py_sym("PyTuple_SetItem"))(t, i, ccpl_py_from(args[i]));
    r = ((ccpl_py_call_t)ccpl_py_sym("PyObject_Call"))(f, t, 0);
    ((ccpl_py_decref_t)ccpl_py_sym("Py_DecRef"))(f);
    ((ccpl_py_decref_t)ccpl_py_sym("Py_DecRef"))(t);
    if (!r) ccpl_py_fail("call failed");
    v = ccpl_py_to(r);
    ((ccpl_py_decref_t)ccpl_py_sym("Py_DecRef"))(r);
    return v;
}

/* Evaluate a Python expression and convert the result. */
static cval ccpl_py_eval(const char *code) {
    PyObject *m, *d, *r;
    cval v;
    ccpl_py_init();
    m = ((ccpl_py_addmod_t)ccpl_py_sym("PyImport_AddModule"))("__main__");
    if (!m) ccpl_py_fail("cannot access __main__");
    d = ((ccpl_py_moddict_t)ccpl_py_sym("PyModule_GetDict"))(m);
    r = ((ccpl_py_run_t)ccpl_py_sym("PyRun_String"))(code, CCPL_PY_EVAL_INPUT, d, d);
    if (!r) ccpl_py_fail("evaluation failed");
    v = ccpl_py_to(r);
    ((ccpl_py_decref_t)ccpl_py_sym("Py_DecRef"))(r);
    return v;
}

/* Execute a block of Python statements. */
static cval ccpl_py_exec(const char *code) {
    PyObject *m, *d, *r;
    ccpl_py_init();
    m = ((ccpl_py_addmod_t)ccpl_py_sym("PyImport_AddModule"))("__main__");
    if (!m) ccpl_py_fail("cannot access __main__");
    d = ((ccpl_py_moddict_t)ccpl_py_sym("PyModule_GetDict"))(m);
    r = ((ccpl_py_run_t)ccpl_py_sym("PyRun_String"))(code, CCPL_PY_FILE_INPUT, d, d);
    if (!r) ccpl_py_fail("execution failed");
    ((ccpl_py_decref_t)ccpl_py_sym("Py_DecRef"))(r);
    return cvnil();
}

#endif /* CCPL_RUNTIME_H */
