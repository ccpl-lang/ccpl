/* coolc - compiler for CCPL (Cool Compilable Programming Language).
 * Lua-like syntax, mixed dynamic + native low-level programming.
 * Emits C code compiled by TinyCC into a native executable.
 * No interpreter anywhere. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <windows.h>

#define COOLC_VERSION "1.0"

/* ---------------------------------- utils --------------------------------- */

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "coolc: out of memory\n"); exit(1); }
    return p;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

static void fail(int line, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "coolc: line %d: ", line);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

typedef struct Vec { void **d; int n, cap; } Vec;

static void vec_push(Vec *v, void *p) {
    if (v->n == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 16;
        v->d = realloc(v->d, (size_t)v->cap * sizeof(void *));
        if (!v->d) fail(0, "out of memory");
    }
    v->d[v->n++] = p;
}

static Vec *new_vec(void) {
    Vec *v = xmalloc(sizeof(Vec));
    memset(v, 0, sizeof(Vec));
    return v;
}

typedef struct Buf { char *s; int n, cap; } Buf;

static void buf_grow(Buf *b, int need) {
    if (b->n + need + 1 > b->cap) {
        while (b->n + need + 1 > b->cap) b->cap = b->cap ? b->cap * 2 : 8192;
        b->s = realloc(b->s, (size_t)b->cap);
        if (!b->s) fail(0, "out of memory");
    }
}

static void buf_put(Buf *b, const char *s) {
    int l = (int)strlen(s);
    buf_grow(b, l);
    memcpy(b->s + b->n, s, (size_t)l);
    b->n += l;
    b->s[b->n] = 0;
}

static void buf_putf(Buf *b, const char *fmt, ...) {
    va_list ap;
    int need;
    va_start(ap, fmt);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) fail(0, "format error");
    buf_grow(b, need);
    va_start(ap, fmt);
    vsnprintf(b->s + b->n, (size_t)(b->cap - b->n), fmt, ap);
    va_end(ap);
    b->n += need;
    b->s[b->n] = 0;
}

static char *read_file_bytes(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = xmalloc((size_t)len + 1);
    if (len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        return NULL;
    }
    buf[len] = 0;
    *out_len = len;
    fclose(f);
    return buf;
}

/* -------------------------------- lexer ----------------------------------- */

typedef enum { TK_NUM, TK_STR, TK_NAME, TK_KW, TK_OP, TK_NL, TK_EOF } TokKind;

typedef struct Token {
    TokKind kind;
    int line;
    double num;
    int is_int;
    char *text;
} Token;

typedef struct Lexer {
    const char *src;
    int pos, line;
    Token *tok;
    int ntok, cap;
} Lexer;

static const char *KEYWORDS[] = {
    "var", "variable", "func", "end", "if", "then", "elseif", "else", "while",
    "do", "repeat", "until", "for", "return", "break", "continue",
    "get", "and", "or", "not", "true", "false", "nil",
    "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
    "f32", "f64", "bool", "char", "ptr", NULL
};

static int is_kw(const char *s) {
    for (int i = 0; KEYWORDS[i]; i++)
        if (!strcmp(KEYWORDS[i], s)) return 1;
    return 0;
}

static void tk_push(Lexer *l, TokKind kind, int line, double num, int is_int, const char *txt) {
    if (l->ntok == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 64;
        l->tok = realloc(l->tok, (size_t)l->cap * sizeof(Token));
        if (!l->tok) fail(0, "out of memory");
    }
    Token *t = &l->tok[l->ntok++];
    t->kind = kind;
    t->line = line;
    t->num = num;
    t->is_int = is_int;
    t->text = txt ? xstrdup(txt) : NULL;
}

static char *decode_string(const char *s, int len, int line) {
    char *out = xmalloc((size_t)len + 1);
    int o = 0, i = 0;
    while (i < len) {
        char c = s[i];
        if (c == '\\' && i + 1 < len) {
            char e = s[i + 1];
            if (isdigit((unsigned char)e)) {
                int v = 0, k = 0;
                while (i + 1 < len && isdigit((unsigned char)s[i + 1]) && k < 3) {
                    v = v * 10 + (s[i + 1] - '0');
                    i++; k++;
                }
                out[o++] = (char)(v & 0xFF);
                i += 1;
            } else {
                switch (e) {
                    case 'n': out[o++] = '\n'; break;
                    case 't': out[o++] = '\t'; break;
                    case 'r': out[o++] = '\r'; break;
                    case 'a': out[o++] = '\a'; break;
                    case 'b': out[o++] = '\b'; break;
                    case 'f': out[o++] = '\f'; break;
                    case 'v': out[o++] = '\v'; break;
                    case '0': out[o++] = '\0'; break;
                    case '\\': out[o++] = '\\'; break;
                    case '"': out[o++] = '"'; break;
                    default: fail(line, "bad escape sequence \\%c", e);
                }
                i += 2;
            }
        } else {
            out[o++] = c;
            i++;
        }
    }
    for (int j = 0; j < o; j++)
        if (out[j] == 0) fail(line, "NUL byte in string literal is not supported");
    out[o] = 0;
    return out;
}

static void lex(Lexer *l, const char *src) {
    l->src = src;
    l->pos = 0;
    l->line = 1;
    while (1) {
        char c = src[l->pos];
        if (c == 0) { tk_push(l, TK_EOF, l->line, 0, 0, NULL); return; }
        if (c == '\n') {
            l->pos++;
            l->line++;
            tk_push(l, TK_NL, l->line, 0, 0, NULL);
            continue;
        }
        if (c == '\r' || c == ' ' || c == '\t' || c == '\f' || c == '\v') { l->pos++; continue; }
        if (c == '-' && src[l->pos + 1] == '-') {
            if (src[l->pos + 2] == '[' && src[l->pos + 3] == '[') {
                const char *end = strstr(src + l->pos + 4, "]]");
                if (!end) fail(l->line, "unterminated block comment");
                for (const char *p = src + l->pos; p < end + 2; p++)
                    if (*p == '\n') l->line++;
                l->pos = (int)(end + 2 - src);
            } else {
                while (src[l->pos] && src[l->pos] != '\n') l->pos++;
            }
            continue;
        }
        if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)src[l->pos + 1]))) {
            int line = l->line, start = l->pos, is_int = 1;
            if (c == '0' && (src[l->pos + 1] == 'x' || src[l->pos + 1] == 'X')) {
                l->pos += 2;
                while (isxdigit((unsigned char)src[l->pos])) l->pos++;
                char tmp[64];
                int len = l->pos - start;
                memcpy(tmp, src + start, (size_t)len); tmp[len] = 0;
                tk_push(l, TK_NUM, line, (double)_strtoi64(tmp, NULL, 16), 1, NULL);
                continue;
            }
            while (isdigit((unsigned char)src[l->pos])) l->pos++;
            if (src[l->pos] == '.' && src[l->pos + 1] != '.') {
                is_int = 0;
                l->pos++;
                while (isdigit((unsigned char)src[l->pos])) l->pos++;
            }
            if (src[l->pos] == 'e' || src[l->pos] == 'E') {
                const char *q = src + l->pos + 1;
                if (*q == '+' || *q == '-') q++;
                if (isdigit((unsigned char)*q)) {
                    is_int = 0;
                    l->pos = (int)(q - src);
                    while (isdigit((unsigned char)src[l->pos])) l->pos++;
                }
            }
            char tmp[128];
            int len = l->pos - start;
            memcpy(tmp, src + start, (size_t)len); tmp[len] = 0;
            tk_push(l, TK_NUM, line, strtod(tmp, NULL), is_int, NULL);
            continue;
        }
        if (c == '"') {
            int line = l->line;
            int start = ++l->pos;
            while (src[l->pos] && src[l->pos] != '"' && src[l->pos] != '\n') {
                if (src[l->pos] == '\\') l->pos++;
                l->pos++;
            }
            if (src[l->pos] != '"') fail(line, "unterminated string literal");
            char *val = decode_string(src + start, l->pos - start, line);
            l->pos++;
            tk_push(l, TK_STR, line, 0, 0, val);
            free(val);
            continue;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            int line = l->line, start = l->pos;
            while (isalnum((unsigned char)src[l->pos]) || src[l->pos] == '_') l->pos++;
            int len = l->pos - start;
            if (len >= 128) len = 127;
            char tmp[128];
            memcpy(tmp, src + start, (size_t)len); tmp[len] = 0;
            tk_push(l, is_kw(tmp) ? TK_KW : TK_NAME, line, 0, 0, tmp);
            continue;
        }
        {
            int line = l->line;
            const char *two = NULL;
            if (c == '.' && src[l->pos + 1] == '.') two = "..";
            else if (c == '=' && src[l->pos + 1] == '=') two = "==";
            else if (c == '~' && src[l->pos + 1] == '=') two = "~=";
            else if (c == '<' && src[l->pos + 1] == '=') two = "<=";
            else if (c == '>' && src[l->pos + 1] == '=') two = ">=";
            if (two) { tk_push(l, TK_OP, line, 0, 0, two); l->pos += 2; continue; }
            char one[2] = {c, 0};
            if (strchr("()[]{},;=<>+-*/%&:", c)) {
                tk_push(l, TK_OP, line, 0, 0, one);
                l->pos++;
                continue;
            }
            fail(line, "unexpected character '%c'", c);
        }
    }
}

/* ------------------------------- AST nodes -------------------------------- */

typedef struct Node Node;
typedef struct Branch { Node *cond; Vec *body; } Branch;

typedef enum {
    N_NUM, N_STR, N_NAME, N_NIL, N_BOOL,
    N_UNOP, N_BINOP, N_CALL, N_CAST, N_DEREF, N_ADDR, N_INDEX,
    N_LOCAL, N_ASSIGN, N_EXPR,
    N_IF, N_WHILE, N_REPEAT, N_FOR, N_RETURN, N_BREAK, N_CONTINUE,
    N_FUNCDEF, N_GET
} NodeKind;

struct Node {
    NodeKind k;
    int line;
    union {
        struct { double num; int is_int; } num;
        char *text;
        struct { int v; } boolean;
        struct { const char *op; Node *a; } unop;
        struct { const char *op; Node *l, *r; } binop;
        struct { char *fname; Vec *args; } call;
        struct { const char *typ; Node *a; } cast;
        struct { Node *a; } unary;
        struct { Node *base, *idx; } index;
        struct { char *name; char *typ; Node *value; } declar;
        struct { Node *lhs; Node *value; } assign;
        struct { Node *e; } expr;
        struct { Vec *branches; Vec *elsep; } ifn;
        struct { Node *cond; Vec *body; } whil;
        struct { Vec *body; Node *cond; } rep;
        struct { char *var; Node *start, *limit, *step; Vec *body; } forn;
        struct { Node *value; } ret;
        struct { char *name; Vec *params; Vec *body; char *rettype; } func;
    } u;
};

static Node *mk(NodeKind k, int line) {
    Node *n = xmalloc(sizeof(Node));
    memset(n, 0, sizeof(Node));
    n->k = k;
    n->line = line;
    return n;
}

/* -------------------------------- parser ---------------------------------- */

typedef struct Parser { Token *tok; int n, i; } Parser;

static Token *peek(Parser *p, int off) {
    int j = p->i + off;
    if (j >= p->n) j = p->n - 1;
    return &p->tok[j];
}

static Token *nextt(Parser *p) { return &p->tok[p->i++]; }

static int is_op(Parser *p, const char *s) {
    Token *t = peek(p, 0);
    return t->kind == TK_OP && !strcmp(t->text, s);
}

static int is_kwe(Parser *p, const char *s) {
    Token *t = peek(p, 0);
    return t->kind == TK_KW && !strcmp(t->text, s);
}

static void expect_op(Parser *p, const char *s) {
    if (!is_op(p, s))
        fail(peek(p, 0)->line, "expected '%s', got '%s'", s, peek(p, 0)->text);
    nextt(p);
}

static void skip_seps(Parser *p) {
    while (p->i < p->n) {
        Token *t = peek(p, 0);
        if (t->kind == TK_NL) { p->i++; continue; }
        if (t->kind == TK_OP && !strcmp(t->text, ";")) { p->i++; continue; }
        return;
    }
}

static Node *parse_stmt(Parser *p);
static Vec *parse_block(Parser *p);
static Node *parse_expr(Parser *p, int min_prec);
static Node *parse_unary(Parser *p);
static Node *mk_deref(Token *t, Parser *p);
static Node *mk_addr(Token *t, Parser *p);

/* native type keywords -> C types */
static const char *type_cname(const char *kw) {
    if (!strcmp(kw, "i8")) return "int8_t";
    if (!strcmp(kw, "i16")) return "int16_t";
    if (!strcmp(kw, "i32")) return "int32_t";
    if (!strcmp(kw, "i64")) return "int64_t";
    if (!strcmp(kw, "u8")) return "uint8_t";
    if (!strcmp(kw, "u16")) return "uint16_t";
    if (!strcmp(kw, "u32")) return "uint32_t";
    if (!strcmp(kw, "u64")) return "uint64_t";
    if (!strcmp(kw, "f32")) return "float";
    if (!strcmp(kw, "f64")) return "double";
    if (!strcmp(kw, "bool")) return "bool";
    if (!strcmp(kw, "char")) return "char";
    if (!strcmp(kw, "ptr")) return "void";
    return NULL;
}

static int is_type_kw(Parser *p, Token *t) {
    return t->kind == TK_KW && type_cname(t->text) != NULL;
}

/* current token must be a type keyword; parses optional '*' suffixes */
static char *parse_type(Parser *p) {
    Token *t = nextt(p);
    const char *base = type_cname(t->text);
    if (!base) fail(t->line, "expected type, got '%s'", t->text);
    Buf b = {NULL, 0, 0};
    buf_put(&b, base);
    while (is_op(p, "*")) {
        nextt(p);
        buf_put(&b, " *");
    }
    return b.s;
}

static int expr_start(Parser *p) {
    Token *t = peek(p, 0);
    if (t->kind == TK_NUM || t->kind == TK_STR || t->kind == TK_NAME) return 1;
    if (t->kind == TK_OP && (t->text[0] == '(' || t->text[0] == '-' || t->text[0] == '+' ||
                             t->text[0] == '*' || t->text[0] == '&')) return 1;
    if (t->kind == TK_KW && (!strcmp(t->text, "not") || !strcmp(t->text, "nil") ||
                             !strcmp(t->text, "true") || !strcmp(t->text, "false"))) return 1;
    return 0;
}

/* handle "(i32)x ..." casts and "(*ptr)" grouping */
static Node *parse_postfix(Parser *p);

static Node *parse_cast_or_paren(Parser *p) {
    /* current token is '(' */
    Token *lp = peek(p, 0);
    Token *n1 = peek(p, 1);
    if (is_type_kw(p, n1)) {
        nextt(p);                    /* '(' */
        char *typ = parse_type(p);
        if (!is_op(p, ")")) fail(peek(p, 0)->line, "expected ')' to close cast");
        nextt(p);
        Node *operand = parse_unary(p);
        Node *n = mk(N_CAST, lp->line);
        n->u.cast.typ = typ;
        n->u.cast.a = operand;
        return n;
    }
    nextt(p);                        /* '(' group */
    Node *e = parse_expr(p, 0);
    expect_op(p, ")");
    return e;
}

static Node *parse_unary(Parser *p);

static Node *parse_primary(Parser *p) {
    Token *t = nextt(p);
    if (t->kind == TK_NUM) {
        Node *n = mk(N_NUM, t->line);
        n->u.num.num = t->num;
        n->u.num.is_int = t->is_int;
        return n;
    }
    if (t->kind == TK_STR) {
        Node *n = mk(N_STR, t->line);
        n->u.text = t->text;
        return n;
    }
    if (t->kind == TK_NAME) {
        Node *n = mk(N_NAME, t->line);
        n->u.text = t->text;
        return n;
    }
    if (t->kind == TK_OP && t->text[0] == '(') {
        p->i--;                      /* rewind; let cast/paren logic read '(' */
        return parse_cast_or_paren(p);
    }
    if (t->kind == TK_KW) {
        if (!strcmp(t->text, "nil")) return mk(N_NIL, t->line);
        if (!strcmp(t->text, "true") || !strcmp(t->text, "false")) {
            Node *n = mk(N_BOOL, t->line);
            n->u.boolean.v = !strcmp(t->text, "true");
            return n;
        }
    }
    fail(t->line, "expected expression");
    return NULL;
}

static Node *parse_postfix(Parser *p) {
    Node *e = parse_primary(p);
    while (1) {
        if (is_op(p, "(")) {
            int line = peek(p, 0)->line;
            nextt(p);
            if (e->k != N_NAME) fail(line, "can only call named functions");
            Vec *args = new_vec();
            while (!is_op(p, ")")) {
                vec_push(args, parse_expr(p, 0));
                if (is_op(p, ",")) nextt(p);
                else if (!is_op(p, ")")) fail(peek(p, 0)->line, "expected ',' or ')' in call");
            }
            nextt(p);
            Node *n = mk(N_CALL, line);
            n->u.call.fname = e->u.text;
            n->u.call.args = args;
            e = n;
        } else if (is_op(p, "[")) {
            int line = peek(p, 0)->line;
            nextt(p);
            Node *idx = parse_expr(p, 0);
            expect_op(p, "]");
            Node *n = mk(N_INDEX, line);
            n->u.index.base = e;
            n->u.index.idx = idx;
            e = n;
        } else {
            return e;
        }
    }
}

static Node *parse_unary(Parser *p) {
    Token *t = peek(p, 0);
    if (t->kind == TK_OP && t->text[0] == '-') {
        nextt(p);
        Node *n = mk(N_UNOP, t->line);
        n->u.unop.op = "-";
        n->u.unop.a = parse_unary(p);
        return n;
    }
    if (t->kind == TK_OP && t->text[0] == '+') {
        nextt(p);
        return parse_unary(p);
    }
    if (t->kind == TK_OP && t->text[0] == '*') {
        nextt(p);
        return mk_deref(t, p);
    }
    if (t->kind == TK_OP && t->text[0] == '&') {
        nextt(p);
        return mk_addr(t, p);
    }
    if (t->kind == TK_KW && !strcmp(t->text, "not")) {
        nextt(p);
        Node *n = mk(N_UNOP, t->line);
        n->u.unop.op = "not";
        n->u.unop.a = parse_unary(p);
        return n;
    }
    return parse_postfix(p);
}

static Node *mk_deref(Token *t, Parser *p) {
    Node *n = mk(N_DEREF, t->line);
    n->u.unary.a = parse_unary(p);
    return n;
}

static Node *mk_addr(Token *t, Parser *p) {
    Node *n = mk(N_ADDR, t->line);
    n->u.unary.a = parse_unary(p);
    return n;
}

typedef struct { const char *op; int prec; int right; } BinDef;

static const BinDef BINOPS[] = {
    {"or", 1, 0}, {"and", 2, 0},
    {"==", 3, 0}, {"~=", 3, 0}, {"<=", 3, 0}, {">=", 3, 0}, {"<", 3, 0}, {">", 3, 0},
    {"..", 4, 1},
    {"+", 5, 0}, {"-", 5, 0},
    {"*", 6, 0}, {"/", 6, 0}, {"%", 6, 0},
    {NULL, 0, 0}
};

static Node *parse_expr(Parser *p, int min_prec) {
    Node *left = parse_unary(p);
    while (1) {
        Token *t = peek(p, 0);
        const BinDef *op = NULL;
        if (t->kind == TK_OP || t->kind == TK_KW) {
            for (const BinDef *d = BINOPS; d->op; d++) {
                if (!strcmp(d->op, t->text)) { op = d; break; }
            }
        }
        if (!op || op->prec < min_prec) break;
        nextt(p);
        int next_min = op->right ? op->prec : op->prec + 1;
        Node *right = parse_expr(p, next_min);
        Node *n = mk(N_BINOP, t->line);
        n->u.binop.op = op->op;
        n->u.binop.l = left;
        n->u.binop.r = right;
        left = n;
    }
    return left;
}

static Node *parse_local(Parser *p) {
    Token *k = nextt(p);
    Token *next = peek(p, 0);
    char *typ = NULL;
    Node *n = mk(N_LOCAL, k->line);
    if (is_type_kw(p, next)) {
        typ = parse_type(p);
        next = peek(p, 0);
    }
    Token *name = nextt(p);
    if (name->kind != TK_NAME) fail(name->line, "expected variable name after 'var'");
    if (is_op(p, ":")) {
        nextt(p);
        typ = parse_type(p);
    }
    n->u.declar.name = name->text;
    n->u.declar.typ = typ;
    n->u.declar.value = NULL;
    if (is_op(p, "=")) {
        nextt(p);
        n->u.declar.value = parse_expr(p, 0);
    }
    return n;
}

static Node *parse_assign(Parser *p) {
    Node *lhs;
    Token *name;
    int line = peek(p, 0)->line;
    if (is_op(p, "*")) {
        Node *u = parse_unary(p);
        if (u->k != N_DEREF && u->k != N_INDEX) fail(line, "bad assignment target");
        lhs = u;
    } else {
        name = nextt(p);
        if (name->kind != TK_NAME) fail(name->line, "expected variable name");
        lhs = mk(N_NAME, name->line);
        lhs->u.text = name->text;
        if (is_op(p, "[")) {
            nextt(p);
            Node *idx = parse_expr(p, 0);
            expect_op(p, "]");
            Node *n = mk(N_INDEX, lhs->line);
            n->u.index.base = lhs;
            n->u.index.idx = idx;
            lhs = n;
        }
    }
    nextt(p); /* '=' */
    Node *n = mk(N_ASSIGN, line);
    n->u.assign.lhs = lhs;
    n->u.assign.value = parse_expr(p, 0);
    return n;
}

typedef struct { char *name; char *typ; } Param; /* typ NULL = dynamic cval */

static Node *parse_function(Parser *p) {
    Token *k = nextt(p);
    Token *name = nextt(p);
    if (name->kind != TK_NAME) fail(name->line, "expected function name");
    Vec *params = new_vec();
    if (is_op(p, "(")) {
        nextt(p);
        while (!is_op(p, ")")) {
            Token *pa = nextt(p);
            if (pa->kind != TK_NAME) fail(pa->line, "expected parameter name");
            Param *pm = xmalloc(sizeof(Param));
            pm->name = pa->text;
            pm->typ = NULL;
            if (is_op(p, ":")) {
                nextt(p);
                pm->typ = parse_type(p);
            }
            vec_push(params, pm);
            if (is_op(p, ",")) nextt(p);
            else if (!is_op(p, ")")) fail(peek(p, 0)->line, "expected ',' or ')' in parameter list");
        }
        nextt(p);
    }
    char *rettype = NULL;
    if (is_op(p, ":")) {
        nextt(p);
        rettype = parse_type(p);
    }
    Vec *body = parse_block(p);
    if (!is_kwe(p, "end")) fail(name->line, "expected 'end' to close function '%s'", name->text);
    nextt(p);
    Node *n = mk(N_FUNCDEF, k->line);
    n->u.func.name = name->text;
    n->u.func.params = params;
    n->u.func.body = body;
    n->u.func.rettype = rettype;
    return n;
}

static Node *parse_if(Parser *p) {
    Token *k = nextt(p);
    Vec *branches = new_vec();
    while (1) {
        Node *cond = parse_expr(p, 0);
        if (!is_kwe(p, "then")) fail(peek(p, 0)->line, "expected 'then'");
        nextt(p);
        Branch *br = xmalloc(sizeof(Branch));
        br->cond = cond;
        br->body = parse_block(p);
        vec_push(branches, br);
        if (is_kwe(p, "elseif")) { nextt(p); continue; }
        break;
    }
    Vec *elsep = NULL;
    if (is_kwe(p, "else")) {
        nextt(p);
        elsep = parse_block(p);
    }
    if (!is_kwe(p, "end")) fail(peek(p, 0)->line, "expected 'end'");
    nextt(p);
    Node *n = mk(N_IF, k->line);
    n->u.ifn.branches = branches;
    n->u.ifn.elsep = elsep;
    return n;
}

static Node *parse_while(Parser *p) {
    Token *k = nextt(p);
    Node *cond = parse_expr(p, 0);
    if (!is_kwe(p, "do")) fail(peek(p, 0)->line, "expected 'do'");
    nextt(p);
    Vec *body = parse_block(p);
    if (!is_kwe(p, "end")) fail(peek(p, 0)->line, "expected 'end'");
    nextt(p);
    Node *n = mk(N_WHILE, k->line);
    n->u.whil.cond = cond;
    n->u.whil.body = body;
    return n;
}

static Node *parse_repeat(Parser *p) {
    Token *k = nextt(p);
    Vec *body = parse_block(p);
    if (!is_kwe(p, "until")) fail(peek(p, 0)->line, "expected 'until'");
    nextt(p);
    Node *cond = parse_expr(p, 0);
    Node *n = mk(N_REPEAT, k->line);
    n->u.rep.body = body;
    n->u.rep.cond = cond;
    return n;
}

static Node *parse_for(Parser *p) {
    Token *k = nextt(p);
    Token *var = nextt(p);
    if (var->kind != TK_NAME) fail(var->line, "expected loop variable");
    nextt(p); /* '=' */
    Node *start = parse_expr(p, 0);
    if (!is_op(p, ",")) fail(peek(p, 0)->line, "expected ',' in for loop");
    nextt(p);
    Node *limit = parse_expr(p, 0);
    Node *step = NULL;
    if (is_op(p, ",")) {
        nextt(p);
        step = parse_expr(p, 0);
    }
    if (!is_kwe(p, "do")) fail(peek(p, 0)->line, "expected 'do'");
    nextt(p);
    Vec *body = parse_block(p);
    if (!is_kwe(p, "end")) fail(peek(p, 0)->line, "expected 'end'");
    nextt(p);
    Node *n = mk(N_FOR, k->line);
    n->u.forn.var = var->text;
    n->u.forn.start = start;
    n->u.forn.limit = limit;
    n->u.forn.step = step;
    n->u.forn.body = body;
    return n;
}

static Node *parse_return(Parser *p) {
    Token *k = nextt(p);
    Node *value = NULL;
    if (expr_start(p)) value = parse_expr(p, 0);
    Node *n = mk(N_RETURN, k->line);
    n->u.ret.value = value;
    return n;
}

static Node *parse_stmt(Parser *p) {
    Token *t = peek(p, 0);
    if (t->kind == TK_KW) {
        if (!strcmp(t->text, "var") || !strcmp(t->text, "variable")) return parse_local(p);
        if (!strcmp(t->text, "func")) return parse_function(p);
        if (!strcmp(t->text, "if")) return parse_if(p);
        if (!strcmp(t->text, "while")) return parse_while(p);
        if (!strcmp(t->text, "repeat")) return parse_repeat(p);
        if (!strcmp(t->text, "for")) return parse_for(p);
        if (!strcmp(t->text, "return")) return parse_return(p);
        if (!strcmp(t->text, "break")) {
            int line = t->line;
            nextt(p);
            return mk(N_BREAK, line);
        }
        if (!strcmp(t->text, "continue")) {
            int line = t->line;
            nextt(p);
            return mk(N_CONTINUE, line);
        }
        if (!strcmp(t->text, "get")) {
            int line = t->line;
            nextt(p);
            Token *path = nextt(p);
            if (path->kind != TK_STR) fail(path->line, "expected a string path after 'get'");
            Node *n = mk(N_GET, line);
            n->u.text = path->text;
            return n;
        }
        if (!strcmp(t->text, "end") || !strcmp(t->text, "then") || !strcmp(t->text, "do") ||
            !strcmp(t->text, "elseif") || !strcmp(t->text, "else") || !strcmp(t->text, "until"))
            fail(t->line, "unexpected keyword '%s'", t->text);
    }
    {
        Token *n1 = peek(p, 1);
        int is_lvalue = 0;
        if (t->kind == TK_OP && !strcmp(t->text, "*")) is_lvalue = 1;
        else if (t->kind == TK_NAME && n1->kind == TK_OP && !strcmp(n1->text, "=")) is_lvalue = 1;
        else if (t->kind == TK_NAME && n1->kind == TK_OP && !strcmp(n1->text, "[")) is_lvalue = 1;
        if (is_lvalue) {
            return parse_assign(p);
        }
    }
    if (t->kind == TK_NAME || expr_start(p)) {
        Node *n = parse_expr(p, 0);
        if (n->k != N_CALL) fail(t->line, "expression statement must be a function call");
        Node *s = mk(N_EXPR, t->line);
        s->u.expr.e = n;
        return s;
    }
    fail(t->line, "unexpected token '%s'", t->text);
    return NULL;
}

static Vec *parse_block(Parser *p) {
    Vec *stmts = new_vec();
    while (1) {
        skip_seps(p);
        Token *t = peek(p, 0);
        if (t->kind == TK_EOF) fail(t->line, "unexpected end of file (missing 'end')");
        if (t->kind == TK_KW && (!strcmp(t->text, "end") || !strcmp(t->text, "elseif") ||
                                 !strcmp(t->text, "else") || !strcmp(t->text, "until")))
            return stmts;
        vec_push(stmts, parse_stmt(p));
    }
}

static Vec *parse_file(Parser *p) {
    Vec *stmts = new_vec();
    while (1) {
        skip_seps(p);
        Token *t = peek(p, 0);
        if (t->kind == TK_EOF) return stmts;
        vec_push(stmts, parse_stmt(p));
    }
}

/* ------------------------------ get / import ------------------------------ */

static char **g_loaded = NULL;
static int g_nloaded = 0;

static int loaded_abs(const char *abs) {
    for (int i = 0; i < g_nloaded; i++)
        if (!_stricmp(g_loaded[i], abs)) return 1;
    return 0;
}

static void mark_loaded(const char *abs) {
    g_loaded = realloc(g_loaded, (size_t)(g_nloaded + 1) * sizeof(char *));
    g_loaded[g_nloaded++] = xstrdup(abs);
}

static char *canon_abs(const char *p) {
    char buf[MAX_PATH];
    DWORD r = GetFullPathNameA(p, MAX_PATH, buf, NULL);
    if (r && r < MAX_PATH) return xstrdup(buf);
    return xstrdup(p);
}

static char *join_path(const char *dir, const char *name) {
    Buf b = {NULL, 0, 0};
    if (dir && *dir) {
        buf_put(&b, dir);
        size_t dl = strlen(dir);
        if (dl && dir[dl - 1] != '\\' && dir[dl - 1] != '/') buf_put(&b, "\\");
    }
    buf_put(&b, name);
    return b.s;
}

static char *dir_of(const char *path) {
    char *d = xstrdup(path);
    char *cut = NULL;
    for (char *c = d; *c; c++)
        if (*c == '\\' || *c == '/') cut = c;
    if (cut) *cut = 0;
    else d[0] = 0;
    return d;
}

/* forward decl, defined below */
static Vec *expand_gets(Vec *stmts, const char *dir);

/* directory containing coolc.exe (cached) */
static const char *exe_dir(void) {
    static char dir[MAX_PATH];
    static int done = 0;
    if (!done) {
        DWORD n = GetModuleFileNameA(NULL, dir, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) dir[0] = 0;
        else {
            char *slash = strrchr(dir, '\\');
            if (slash) *slash = 0; else dir[0] = 0;
        }
        done = 1;
    }
    return dir;
}

static int is_abs_path(const char *p) {
    return p[0] == '\\' || p[0] == '/' || (isalpha((unsigned char)p[0]) && p[1] == ':');
}

/* open `name` (optionally appending .ccpl) under root; returns malloc'd path or NULL */
static char *find_under(const char *root, const char *name) {
    char *joined = join_path(root, name);
    long len = 0;
    char *src = read_file_bytes(joined, &len);
    if (!src) {
        size_t jl = strlen(joined);
        if (jl < 5 || _stricmp(joined + jl - 5, ".ccpl")) {
            char *we = xmalloc(jl + 6);
            memcpy(we, joined, jl);
            memcpy(we + jl, ".ccpl", 6);
            free(joined);
            joined = we;
            src = read_file_bytes(joined, &len);
        }
    }
    if (!src) { free(joined); return NULL; }
    free(src);
    return joined;
}

/* resolve `name` under one module root; returns malloc'd canonical path or NULL.
   Tries root\name[.ccpl] (flat convention), then for a bare package name
   root\name\name[.ccpl] and root\name\init[.ccpl] (installed-package convention). */
static char *pkg_candidate(const char *root, const char *name) {
    char *c = find_under(root, name);
    if (c) { char *a = canon_abs(c); free(c); return a; }

    if (strchr(name, '/') || strchr(name, '\\')) return NULL;

    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s\\%s\\%s", root, name, name);
    c = find_under("", p);
    if (c) { char *a = canon_abs(c); free(c); return a; }

    snprintf(p, sizeof p, "%s\\%s\\init", root, name);
    c = find_under("", p);
    if (c) { char *a = canon_abs(c); free(c); return a; }

    return NULL;
}

/* resolve module name against importer dir, then installed package dirs */
static char *resolve_module(const char *name, const char *importer_dir) {
    char *cand;
    if (is_abs_path(name)) {
        cand = find_under("", name);
        if (cand) { char *a = canon_abs(cand); free(cand); return a; }
        return NULL;
    }

    cand = pkg_candidate(importer_dir, name);
    if (cand) return cand;

    char pkg[MAX_PATH];
    snprintf(pkg, sizeof pkg, "%s\\packages", exe_dir());
    cand = pkg_candidate(pkg, name);
    if (cand) return cand;

    snprintf(pkg, sizeof pkg, "%s\\..\\packages", exe_dir());
    cand = pkg_candidate(pkg, name);
    if (cand) return cand;

    return NULL;
}

static void collect_calls_node(Node *n, Vec *out) {
    if (!n) return;
    switch (n->k) {
        case N_CALL:
            vec_push(out, n->u.call.fname);
            for (int i = 0; i < n->u.call.args->n; i++)
                collect_calls_node(n->u.call.args->d[i], out);
            break;
        case N_UNOP: collect_calls_node(n->u.unop.a, out); break;
        case N_BINOP: collect_calls_node(n->u.binop.l, out); collect_calls_node(n->u.binop.r, out); break;
        case N_CAST: collect_calls_node(n->u.cast.a, out); break;
        case N_DEREF: case N_ADDR: collect_calls_node(n->u.unary.a, out); break;
        case N_INDEX: collect_calls_node(n->u.index.base, out); collect_calls_node(n->u.index.idx, out); break;
        case N_LOCAL: collect_calls_node(n->u.declar.value, out); break;
        case N_ASSIGN: collect_calls_node(n->u.assign.value, out); break;
        case N_EXPR: collect_calls_node(n->u.expr.e, out); break;
        case N_RETURN: collect_calls_node(n->u.ret.value, out); break;
        case N_IF:
            for (int i = 0; i < n->u.ifn.branches->n; i++) {
                Branch *b = n->u.ifn.branches->d[i];
                collect_calls_node(b->cond, out);
                for (int j = 0; j < b->body->n; j++) collect_calls_node(b->body->d[j], out);
            }
            if (n->u.ifn.elsep)
                for (int j = 0; j < n->u.ifn.elsep->n; j++) collect_calls_node(n->u.ifn.elsep->d[j], out);
            break;
        case N_WHILE:
            collect_calls_node(n->u.whil.cond, out);
            for (int j = 0; j < n->u.whil.body->n; j++) collect_calls_node(n->u.whil.body->d[j], out);
            break;
        case N_REPEAT:
            for (int j = 0; j < n->u.rep.body->n; j++) collect_calls_node(n->u.rep.body->d[j], out);
            collect_calls_node(n->u.rep.cond, out);
            break;
        case N_FOR:
            collect_calls_node(n->u.forn.start, out);
            collect_calls_node(n->u.forn.limit, out);
            collect_calls_node(n->u.forn.step, out);
            for (int j = 0; j < n->u.forn.body->n; j++) collect_calls_node(n->u.forn.body->d[j], out);
            break;
        case N_FUNCDEF:
            for (int j = 0; j < n->u.func.body->n; j++) collect_calls_node(n->u.func.body->d[j], out);
            break;
        default: break;
    }
}

static int name_in(Vec *names, const char *s) {
    for (int i = 0; i < names->n; i++)
        if (!strcmp((char *)names->d[i], s)) return 1;
    return 0;
}

static Node *find_funcdef(Vec *stmts, const char *name) {
    for (int i = 0; i < stmts->n; i++) {
        Node *s = stmts->d[i];
        if (s->k == N_FUNCDEF && !strcmp(s->u.func.name, name)) return s;
    }
    return NULL;
}

/* keep module init statements + only `want` plus module-local funcs it calls */
static Vec *select_func(Vec *stmts, const char *want, int line) {
    Vec *reach = new_vec();
    vec_push(reach, (char *)want);
    for (int idx = 0; idx < reach->n; idx++) {
        const char *fn = reach->d[idx];
        Node *def = find_funcdef(stmts, fn);
        if (!def) fail(line, "module has no function '%s'", want);
        Vec *calls = new_vec();
        for (int j = 0; j < def->u.func.body->n; j++)
            collect_calls_node(def->u.func.body->d[j], calls);
        for (int j = 0; j < calls->n; j++) {
            const char *c = calls->d[j];
            if (find_funcdef(stmts, c) && !name_in(reach, c) && strcmp(c, fn))
                vec_push(reach, (char *)c);
        }
        free(calls->d);
        free(calls);
    }
    Vec *out = new_vec();
    for (int i = 0; i < stmts->n; i++) {
        Node *s = stmts->d[i];
        if (s->k != N_FUNCDEF || name_in(reach, s->u.func.name)) vec_push(out, s);
    }
    free(reach->d);
    free(reach);
    return out;
}

static Vec *load_ccpl(const char *path, const char *dir, int line) {
    char *abs = resolve_module(path, dir);
    const char *func = NULL;
    char *modname = NULL;
    if (!abs) {
        /* maybe "module/func": resolve module part, import a single function */
        const char *slash = NULL;
        for (const char *c = path; *c; c++)
            if (*c == '/' || *c == '\\') slash = c;
        if (slash) {
            size_t ml = (size_t)(slash - path);
            modname = xmalloc(ml + 1);
            memcpy(modname, path, ml);
            modname[ml] = 0;
            func = slash + 1;
            abs = resolve_module(modname, dir);
        }
    }
    if (!abs) fail(line, "cannot find module '%s'", path);

    long len = 0;
    char *src = read_file_bytes(abs, &len);
    if (!src) fail(line, "cannot read module '%s'", abs);

    if (loaded_abs(abs)) {
        free(abs);
        free(modname);
        free(src);
        return new_vec();
    }
    mark_loaded(abs);

    Lexer lx = {0};
    lex(&lx, src);
    Parser p = {lx.tok, lx.ntok, 0};
    Vec *stmts = parse_file(&p);
    if (func) stmts = select_func(stmts, func, line);

    char *subdir = dir_of(abs);
    Vec *out = expand_gets(stmts, subdir);
    free(subdir);
    free(abs);
    free(modname);
    free(src);
    return out;
}

static Vec *expand_gets(Vec *stmts, const char *dir) {
    Vec *out = new_vec();
    for (int i = 0; i < stmts->n; i++) {
        Node *s = stmts->d[i];
        if (s->k == N_GET) {
            Vec *sub = load_ccpl(s->u.text, dir, s->line);
            for (int j = 0; j < sub->n; j++) vec_push(out, sub->d[j]);
            free(sub->d);
            free(sub);
            continue;
        }
        switch (s->k) {
            case N_IF:
                for (int j = 0; j < s->u.ifn.branches->n; j++) {
                    Branch *br = s->u.ifn.branches->d[j];
                    br->body = expand_gets(br->body, dir);
                }
                if (s->u.ifn.elsep) s->u.ifn.elsep = expand_gets(s->u.ifn.elsep, dir);
                break;
            case N_WHILE: s->u.whil.body = expand_gets(s->u.whil.body, dir); break;
            case N_REPEAT: s->u.rep.body = expand_gets(s->u.rep.body, dir); break;
            case N_FOR: s->u.forn.body = expand_gets(s->u.forn.body, dir); break;
            case N_FUNCDEF: s->u.func.body = expand_gets(s->u.func.body, dir); break;
            default: break;
        }
        vec_push(out, s);
    }
    return out;
}

/* --------------------------- function collection -------------------------- */

typedef struct { char *name; Vec *params; Vec *body; char *rettype; } FuncDef;

static Vec *g_funcs = NULL;

static FuncDef *lookup_func(const char *name) {
    if (!g_funcs) return NULL;
    for (int i = 0; i < g_funcs->n; i++) {
        FuncDef *f = g_funcs->d[i];
        if (!strcmp(f->name, name)) return f;
    }
    return NULL;
}

/* mangle user function names so they cannot collide with C keywords/libc */
static char *mangle(const char *name) {
    char *s = xmalloc(strlen(name) + 9);
    sprintf(s, "ccpl_fn_%s", name);
    return s;
}

static void collect_walk(Vec *funcs, Vec *stmts);

static int is_builtin_func(const char *name) {
    static const char *B[] = {
        "say", "oh", "hey",
        "tonumber", "tostring",
        "malloc", "calloc", "realloc", "free", NULL
    };
    for (int i = 0; B[i]; i++)
        if (!strcmp(B[i], name)) return 1;
    return 0;
}

static void collect_add(Vec *funcs, Node *n) {
    char *name = n->u.func.name;
    if (is_builtin_func(name))
        fail(n->line, "'%s' is a builtin and cannot be redefined", name);
    for (int i = 0; i < funcs->n; i++) {
        FuncDef *f = funcs->d[i];
        if (!strcmp(f->name, name)) fail(n->line, "duplicate function '%s'", name);
    }
    FuncDef *f = xmalloc(sizeof(FuncDef));
    f->name = name;
    f->params = n->u.func.params;
    f->body = n->u.func.body;
    f->rettype = n->u.func.rettype;
    vec_push(funcs, f);
    collect_walk(funcs, n->u.func.body);
}

static void collect_walk(Vec *funcs, Vec *stmts) {
    for (int i = 0; i < stmts->n; i++) {
        Node *s = stmts->d[i];
        switch (s->k) {
            case N_FUNCDEF: collect_add(funcs, s); break;
            case N_IF:
                for (int j = 0; j < s->u.ifn.branches->n; j++) {
                    Branch *br = s->u.ifn.branches->d[j];
                    collect_walk(funcs, br->body);
                }
                if (s->u.ifn.elsep) collect_walk(funcs, s->u.ifn.elsep);
                break;
            case N_WHILE: collect_walk(funcs, s->u.whil.body); break;
            case N_REPEAT: collect_walk(funcs, s->u.rep.body); break;
            case N_FOR: collect_walk(funcs, s->u.forn.body); break;
            default: break;
        }
    }
}

/* -------------------------------- codegen --------------------------------- */

typedef struct { char *name; char *cname; char *ctype; } Sym;

static Vec *sym_table = NULL;
static int *scope_mark = NULL;
static int scope_top = -1;

static void scope_push(void) {
    scope_top++;
    scope_mark = realloc(scope_mark, (size_t)(scope_top + 1) * sizeof(int));
    scope_mark[scope_top] = sym_table->n;
}

static void scope_pop(void) {
    sym_table->n = scope_mark[scope_top];
    scope_top--;
}

static void scope_declare(const char *name, const char *cname, const char *ctype) {
    Sym *s = xmalloc(sizeof(Sym));
    s->name = xstrdup(name);
    s->cname = xstrdup(cname);
    s->ctype = ctype ? xstrdup(ctype) : NULL;
    vec_push(sym_table, s);
}

static Sym *scope_lookup(const char *name) {
    for (int i = sym_table->n - 1; i >= 0; i--) {
        Sym *s = sym_table->d[i];
        if (!strcmp(s->name, name)) return s;
    }
    return NULL;
}

/* expression value classes */
enum { CL_CVAL = 0, CL_NUM = 1, CL_PTR = 2, CL_BOOL = 3 };

static int is_numlike(int cls) { return cls == CL_NUM || cls == CL_BOOL; }

static int class_of_ctype(const char *ctype) {
    if (!ctype) return CL_CVAL;
    if (strchr(ctype, '*') || !strcmp(ctype, "void")) return CL_PTR;
    return CL_NUM;
}

static int cg_counter = 0;
static int loop_depth = 0;
static int ind = 0;
static const char *cur_rettype = NULL;

static void write_indent(Buf *out) {
    for (int i = 0; i < ind; i++) buf_put(out, "  ");
}

static void write_endline(Buf *out) {
    buf_put(out, "\n");
}

static void linef(Buf *out, const char *fmt, ...) {
    va_list ap;
    write_indent(out);
    va_start(ap, fmt);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    buf_grow(out, need);
    va_start(ap, fmt);
    vsnprintf(out->s + out->n, (size_t)(out->cap - out->n), fmt, ap);
    va_end(ap);
    out->n += need;
    write_endline(out);
}

static void esc_string_to(const char *s, Buf *out) {
    buf_put(out, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char b = *p;
        if (b == '\\') buf_put(out, "\\\\");
        else if (b == '"') buf_put(out, "\\\"");
        else if (b == '\n') buf_put(out, "\\n");
        else if (b == '\t') buf_put(out, "\\t");
        else if (b == '\r') buf_put(out, "\\r");
        else if (b < 0x20 || b > 0x7E) buf_putf(out, "\\%03o", b);
        else { char c[2] = {(char)b, 0}; buf_put(out, c); }
    }
    buf_put(out, "\"");
}

static const char *dbl_lit(double d) {
    static char buf[64];
    snprintf(buf, sizeof buf, "%.17g", d);
    int l = (int)strlen(buf);
    int has = 0;
    for (int i = 0; i < l; i++)
        if (buf[i] == '.' || buf[i] == 'e' || buf[i] == 'E') { has = 1; break; }
    if (!has) { buf[l] = '.'; buf[l + 1] = '0'; buf[l + 2] = 0; }
    return buf;
}

static char *expr_c(Node *n, int *cls);
static void stmt(Buf *out, Node *n, int func_body);

/* wrap a native expression into a dynamic cval (heap string) */
static char *as_cval(const char *e, int cls) {
    Buf b = {NULL, 0, 0};
    if (cls == CL_CVAL) buf_put(&b, e);
    else if (cls == CL_PTR) buf_putf(&b, "cvnum((double)(intptr_t)(%s))", e);
    else if (cls == CL_BOOL) buf_putf(&b, "cvbool((double)(%s))", e);
    else buf_putf(&b, "cvnum((double)(%s))", e);
    return b.s;
}

static void block_gen(Buf *out, Vec *body, int func_body) {
    for (int i = 0; i < body->n; i++) stmt(out, body->d[i], func_body);
}

static void gen_pack(Buf *out, const char *fn, Vec *args) {
    if (args->n == 0) {
        if (!strcmp(fn, "say"))
            linef(out, "ccpl_print(0, NULL);");
        else
            linef(out, "ccpl_warn(0, NULL);");
        return;
    }
    char arr[64];
    snprintf(arr, sizeof arr, "pp_%d", cg_counter++);
    linef(out, "cval %s[%d];", arr, args->n);
    for (int i = 0; i < args->n; i++) {
        int cls;
        char *e = expr_c(args->d[i], &cls);
        char *w = as_cval(e, cls);
        write_indent(out);
        buf_putf(out, "%s[%d] = %s;", arr, i, w);
        write_endline(out);
        free(e);
        free(w);
    }
    if (!strcmp(fn, "say"))
        linef(out, "ccpl_print(%d, %s);", args->n, arr);
    else
        linef(out, "ccpl_warn(%d, %s);", args->n, arr);
}

static char *expr_c(Node *n, int *cls) {
    Buf b = {NULL, 0, 0};
    switch (n->k) {
        case N_NUM:
            if (n->u.num.is_int) {
                buf_putf(&b, "%lld", (long long)n->u.num.num);
                if (n->u.num.num > 2147483647.0 || n->u.num.num < -2147483648.0)
                    buf_put(&b, "LL");
            } else {
                buf_put(&b, dbl_lit(n->u.num.num));
            }
            *cls = CL_NUM;
            break;
        case N_STR:
            buf_put(&b, "cvstr(");
            esc_string_to(n->u.text, &b);
            buf_put(&b, ")");
            *cls = CL_CVAL;
            break;
        case N_NIL:
            buf_put(&b, "cvnil()");
            *cls = CL_CVAL;
            break;
        case N_BOOL:
            buf_putf(&b, "%d", n->u.boolean.v ? 1 : 0);
            *cls = CL_BOOL;
            break;
        case N_NAME: {
            Sym *s = scope_lookup(n->u.text);
            if (!s) fail(n->line, "undefined variable '%s'", n->u.text);
            buf_put(&b, s->cname);
            *cls = class_of_ctype(s->ctype);
            break;
        }
        case N_UNOP: {
            int ac;
            char *a = expr_c(n->u.unop.a, &ac);
            if (!strcmp(n->u.unop.op, "-")) {
                if (ac == CL_CVAL) {
                    buf_putf(&b, "cv_neg(%s)", a);
                    *cls = CL_CVAL;
                } else {
                    buf_putf(&b, "(-(%s))", a);
                    *cls = CL_NUM;
                }
            } else {
                if (ac == CL_CVAL) {
                    buf_putf(&b, "cv_not(%s)", a);
                    *cls = CL_CVAL;
                } else {
                    buf_putf(&b, "(!(%s))", a);
                    *cls = CL_BOOL;
                }
            }
            free(a);
            break;
        }
        case N_CAST: {
            int ac;
            char *a = expr_c(n->u.cast.a, &ac);
            if (ac == CL_CVAL) {
                fail(n->line, "cannot cast a dynamic value to native type '%s'", n->u.cast.typ);
            }
            buf_putf(&b, "((%s)(%s))", n->u.cast.typ, a);
            *cls = class_of_ctype(n->u.cast.typ);
            free(a);
            break;
        }
        case N_DEREF: {
            int ac;
            char *a = expr_c(n->u.unary.a, &ac);
            if (ac != CL_PTR) fail(n->line, "'*' requires a pointer, got a non-pointer value");
            buf_putf(&b, "(*%s)", a);
            *cls = CL_NUM;   /* best-effort; real type comes from C */
            free(a);
            break;
        }
        case N_ADDR: {
            int ac;
            char *a = expr_c(n->u.unary.a, &ac);
            if (ac == CL_CVAL) fail(n->line, "cannot take the address of a dynamic value");
            buf_putf(&b, "(&%s)", a);
            *cls = CL_PTR;
            free(a);
            break;
        }
        case N_INDEX: {
            int bc, ic;
            char *base = expr_c(n->u.index.base, &bc);
            char *idx = expr_c(n->u.index.idx, &ic);
            if (bc != CL_PTR) fail(n->line, "'[]' requires a pointer base");
            if (ic == CL_CVAL) fail(n->line, "index expression must be native");
            buf_putf(&b, "(%s)[%s]", base, idx);
            *cls = CL_NUM;
            free(base);
            free(idx);
            break;
        }
        case N_BINOP: {
            const char *op = n->u.binop.op;
            int lc, rc;
            char *L = expr_c(n->u.binop.l, &lc);
            char *R = expr_c(n->u.binop.r, &rc);
            if (lc == CL_CVAL || rc == CL_CVAL) {
                if (lc == CL_PTR || rc == CL_PTR)
                    fail(n->line, "cannot mix pointers with dynamic values");
                if (lc != CL_CVAL) {
                    char *w = as_cval(L, lc);
                    free(L);
                    L = w;
                    lc = CL_CVAL;
                }
                if (rc != CL_CVAL) {
                    char *w = as_cval(R, rc);
                    free(R);
                    R = w;
                    rc = CL_CVAL;
                }
                if (!strcmp(op, "and") || !strcmp(op, "or")) {
                    if (!strcmp(op, "and"))
                        buf_putf(&b, "(cvtruthy(%s) ? %s : %s)", L, R, L);
                    else
                        buf_putf(&b, "(cvtruthy(%s) ? %s : %s)", L, L, R);
                    *cls = CL_CVAL;
                } else if (!strcmp(op, "..")) {
                    buf_putf(&b, "cv_concat(%s, %s)", L, R);
                    *cls = CL_CVAL;
                } else {
                    const char *fn = NULL;
                    if (!strcmp(op, "+")) fn = "cv_add";
                    else if (!strcmp(op, "-")) fn = "cv_sub";
                    else if (!strcmp(op, "*")) fn = "cv_mul";
                    else if (!strcmp(op, "/")) fn = "cv_div";
                    else if (!strcmp(op, "%")) fn = "cv_mod";
                    else if (!strcmp(op, "==")) fn = "cv_eq";
                    else if (!strcmp(op, "~=")) fn = "cv_ne";
                    else if (!strcmp(op, "<")) fn = "cv_lt";
                    else if (!strcmp(op, "<=")) fn = "cv_le";
                    else if (!strcmp(op, ">")) fn = "cv_gt";
                    else if (!strcmp(op, ">=")) fn = "cv_ge";
                    if (!fn) fail(n->line, "bad binary op '%s'", op);
                    buf_putf(&b, "%s(%s, %s)", fn, L, R);
                    *cls = CL_CVAL;
                }
            } else {
                /* native raw arithmetic */
                if (!strcmp(op, "and")) {
                    buf_putf(&b, "((%s) ? (%s) : (%s))", L, R, L);
                    *cls = (lc == CL_BOOL && rc == CL_BOOL) ? CL_BOOL : CL_NUM;
                } else if (!strcmp(op, "or")) {
                    buf_putf(&b, "((%s) ? (%s) : (%s))", L, L, R);
                    *cls = (lc == CL_BOOL && rc == CL_BOOL) ? CL_BOOL : CL_NUM;
                } else if (!strcmp(op, "..")) {
                    char *wl = as_cval(L, lc);
                    char *wr = as_cval(R, rc);
                    buf_putf(&b, "cv_concat(%s, %s)", wl, wr);
                    *cls = CL_CVAL;
                    free(wl);
                    free(wr);
                } else if (!strcmp(op, "%")) {
                    buf_putf(&b, "ccpl_fmod((double)(%s), (double)(%s))", L, R);
                    *cls = CL_NUM;
                } else {
                    const char *raw = NULL;
                    int cmp = 0;
                    if (!strcmp(op, "+") || !strcmp(op, "-") || !strcmp(op, "*") ||
                        !strcmp(op, "/")) raw = op;
                    else if (!strcmp(op, "==")) { raw = "=="; cmp = 1; }
                    else if (!strcmp(op, "~=")) { raw = "!="; cmp = 1; }
                    else if (!strcmp(op, "<")) { raw = "<"; cmp = 1; }
                    else if (!strcmp(op, "<=")) { raw = "<="; cmp = 1; }
                    else if (!strcmp(op, ">")) { raw = ">"; cmp = 1; }
                    else if (!strcmp(op, ">=")) { raw = ">="; cmp = 1; }
                    if (!raw) fail(n->line, "bad binary op '%s'", op);
                    buf_putf(&b, "(%s %s %s)", L, raw, R);
                    if (cmp) *cls = CL_BOOL;
                    else if (lc == CL_PTR || rc == CL_PTR) *cls = CL_PTR;
                    else *cls = CL_NUM;
                }
            }
            free(L);
            free(R);
            break;
        }
        case N_CALL: {
            char *fname = n->u.call.fname;
            Vec *args = n->u.call.args;
            FuncDef *fd = lookup_func(fname);
            int is_mem = !strcmp(fname, "malloc") || !strcmp(fname, "calloc") || !strcmp(fname, "realloc");
            Buf al = {NULL, 0, 0};
            for (int i = 0; i < args->n; i++) {
                int ac;
                char *a = expr_c(args->d[i], &ac);
                if (i) buf_put(&al, ", ");
                if (is_mem) {
                    if (ac == CL_CVAL)
                        buf_putf(&al, "(size_t)cvtod(%s)", a);
                    else
                        buf_putf(&al, "(size_t)(%s)", a);
                } else if (!strcmp(fname, "free")) {
                    if (ac != CL_PTR) fail(n->line, "free() expects a pointer");
                    buf_put(&al, a);
                } else if (fd && i < fd->params->n && ((Param *)fd->params->d[i])->typ) {
                    Param *pm = fd->params->d[i];
                    if (ac == CL_CVAL) {
                        if (class_of_ctype(pm->typ) == CL_PTR)
                            fail(n->line, "argument %d of '%s' expects a native pointer of type '%s'", i + 1, fname, pm->typ);
                        buf_putf(&al, "(%s)cvtod(%s)", pm->typ, a);
                    } else {
                        buf_putf(&al, "(%s)(%s)", pm->typ, a);
                    }
                } else {
                    char *w = as_cval(a, ac);
                    buf_put(&al, w);
                    free(w);
                }
                free(a);
            }
            const char *as = al.s ? al.s : "";
            if (!strcmp(fname, "say") || !strcmp(fname, "hey")) {
                fail(n->line, "%s() can only be called as a statement", fname);
            } else if (is_mem) {
                if ((!strcmp(fname, "malloc") || !strcmp(fname, "free")) && args->n != 1)
                    fail(n->line, "%s() expects 1 argument", fname);
                if ((!strcmp(fname, "calloc") || !strcmp(fname, "realloc")) && args->n != 2)
                    fail(n->line, "%s() expects 2 arguments", fname);
                buf_putf(&b, "%s(%s)", fname, as);
                *cls = CL_PTR;
            } else if (!strcmp(fname, "free")) {
                if (args->n != 1) fail(n->line, "free() expects 1 argument");
                buf_putf(&b, "free(%s)", as);
                *cls = CL_CVAL;
            } else if (!strcmp(fname, "oh") || !strcmp(fname, "tonumber") || !strcmp(fname, "tostring")) {
                if (args->n != 1) fail(n->line, "%s() expects 1 argument", fname);
                const char *cfn = !strcmp(fname, "oh") ? "cv_error"
                                : (!strcmp(fname, "tonumber") ? "cv_tonumber" : "cv_tostring");
                buf_putf(&b, "%s(%s)", cfn, as);
                *cls = CL_CVAL;
            } else if (fd) {
                if (args->n != fd->params->n)
                    fail(n->line, "function '%s' expects %d argument(s), got %d",
                         fname, fd->params->n, args->n);
                char *mf = mangle(fname);
                buf_putf(&b, "%s(%s)", mf, as);
                free(mf);
                *cls = fd->rettype ? class_of_ctype(fd->rettype) : CL_CVAL;
            } else {
                fail(n->line, "undefined function '%s'", fname);
            }
            free(al.s);
            break;
        }
        default:
            fail(n->line, "internal: bad expression node");
    }
    return b.s;
}

static void cond_line(Buf *out, Node *cond, const char *prefix) {
    int cls;
    char *e = expr_c(cond, &cls);
    write_indent(out);
    buf_put(out, prefix);
    if (cls == CL_CVAL)
        buf_putf(out, "cvtruthy(%s)", e);
    else
        buf_put(out, e);
    free(e);
}

static void stmt(Buf *out, Node *n, int func_body) {
    switch (n->k) {
        case N_LOCAL: {
            char cname[64];
            snprintf(cname, sizeof cname, "%s_%d", n->u.declar.name, cg_counter++);
            if (n->u.declar.typ) {
                scope_declare(n->u.declar.name, cname, n->u.declar.typ);
                if (n->u.declar.value) {
                    int vc;
                    char *v = expr_c(n->u.declar.value, &vc);
                    if (vc == CL_CVAL)
                        fail(n->line, "cannot initialize native '%s %s' with a dynamic value; use a cast or raw constants",
                             n->u.declar.typ, n->u.declar.name);
                    linef(out, "%s %s = %s;", n->u.declar.typ, cname, v);
                    free(v);
                } else {
                    linef(out, "%s %s = 0;", n->u.declar.typ, cname);
                }
            } else {
                scope_declare(n->u.declar.name, cname, NULL);
                if (n->u.declar.value) {
                    int vc;
                    char *v = expr_c(n->u.declar.value, &vc);
                    if (is_numlike(vc)) {
                        char *w = as_cval(v, vc);
                        linef(out, "cval %s = %s;", cname, w);
                        free(w);
                    } else if (vc == CL_CVAL) {
                        linef(out, "cval %s = %s;", cname, v);
                    } else {
                        fail(n->line, "cannot store a pointer in a dynamic variable; declare it native");
                    }
                    free(v);
                } else {
                    linef(out, "cval %s = cvnil();", cname);
                }
            }
            break;
        }
        case N_ASSIGN: {
            Node *lhs = n->u.assign.lhs;
            Node *val = n->u.assign.value;
            int vc;
            char *v = expr_c(val, &vc);
            if (lhs->k == N_NAME) {
                Sym *s = scope_lookup(lhs->u.text);
                if (!s) fail(n->line, "cannot assign to undefined variable '%s'", lhs->u.text);
                if (s->ctype) {
                    if (vc == CL_CVAL) {
                        linef(out, "%s = (%s)cvtod(%s);", s->cname, s->ctype, v);
                    } else {
                        linef(out, "%s = %s;", s->cname, v);
                    }
                } else {
                    if (is_numlike(vc)) {
                        char *w = as_cval(v, vc);
                        linef(out, "%s = %s;", s->cname, w);
                        free(w);
                    } else if (vc == CL_CVAL) {
                        linef(out, "%s = %s;", s->cname, v);
                    } else {
                        fail(n->line, "cannot assign a pointer to a dynamic variable");
                    }
                }
            } else {
                int lc;
                char *l = expr_c(lhs, &lc);
                if (vc == CL_CVAL)
                    fail(n->line, "cannot assign a dynamic value to a native memory location; use a cast");
                linef(out, "%s = %s;", l, v);
                free(l);
            }
            free(v);
            break;
        }
        case N_EXPR: {
            Node *e = n->u.expr.e;
            if (e->k == N_CALL && (!strcmp(e->u.call.fname, "say") || !strcmp(e->u.call.fname, "hey"))) {
                gen_pack(out, e->u.call.fname, e->u.call.args);
            } else {
                int c;
                char *ec = expr_c(e, &c);
                write_indent(out);
                buf_putf(out, "(void)(%s);", ec);
                write_endline(out);
                free(ec);
            }
            break;
        }
        case N_IF: {
            Vec *bs = n->u.ifn.branches;
            for (int j = 0; j < bs->n; j++) {
                Branch *br = bs->d[j];
                int cls;
                char *e = expr_c(br->cond, &cls);
                if (j == 0) {
                    write_indent(out);
                    buf_put(out, "if (");
                } else {
                    buf_put(out, "} else if (");
                }
                if (cls == CL_CVAL)
                    buf_putf(out, "cvtruthy(%s)", e);
                else
                    buf_put(out, e);
                buf_put(out, ") {");
                write_endline(out);
                free(e);
                ind++;
                block_gen(out, br->body, func_body);
                ind--;
            }
            if (n->u.ifn.elsep) {
                buf_put(out, "} else {");
                write_endline(out);
                ind++;
                block_gen(out, n->u.ifn.elsep, func_body);
                ind--;
            }
            buf_put(out, "}");
            write_endline(out);
            break;
        }
        case N_WHILE: {
            write_indent(out);
            buf_put(out, "while (");
            {
                int cls;
                char *e = expr_c(n->u.whil.cond, &cls);
                if (cls == CL_CVAL)
                    buf_putf(out, "cvtruthy(%s)", e);
                else
                    buf_put(out, e);
                free(e);
            }
            buf_put(out, ") {");
            write_endline(out);
            ind++;
            loop_depth++;
            block_gen(out, n->u.whil.body, func_body);
            loop_depth--;
            ind--;
            buf_put(out, "}");
            write_endline(out);
            break;
        }
        case N_REPEAT: {
            write_indent(out);
            buf_put(out, "do {");
            write_endline(out);
            ind++;
            loop_depth++;
            block_gen(out, n->u.rep.body, func_body);
            loop_depth--;
            ind--;
            write_indent(out);
            buf_put(out, "} while (!");
            {
                int cls;
                char *e = expr_c(n->u.rep.cond, &cls);
                if (cls == CL_CVAL)
                    buf_putf(out, "cvtruthy(%s)", e);
                else
                    buf_put(out, e);
                free(e);
            }
            buf_put(out, ");");
            write_endline(out);
            break;
        }
        case N_FOR: {
            char var[64], lim[64], stp[64];
            snprintf(var, sizeof var, "%s_%d", n->u.forn.var, cg_counter++);
            snprintf(lim, sizeof lim, "lim_%d", cg_counter++);
            snprintf(stp, sizeof stp, "stp_%d", cg_counter++);
            scope_push();
            scope_declare(n->u.forn.var, var, NULL);
            write_indent(out);
            buf_put(out, "{");
            write_endline(out);
            ind++;
            {
                int vc;
                char *v = expr_c(n->u.forn.start, &vc);
                char *w = as_cval(v, vc);
                linef(out, "cval %s = %s;", var, w);
                free(v);
                free(w);
            }
            {
                int lc;
                char *l = expr_c(n->u.forn.limit, &lc);
                linef(out, "double %s = cvtod(%s);", lim, as_cval(l, lc));
                free(l);
            }
            if (n->u.forn.step) {
                int sc;
                char *s = expr_c(n->u.forn.step, &sc);
                linef(out, "double %s = cvtod(%s);", stp, as_cval(s, sc));
                free(s);
            } else {
                linef(out, "double %s = 1.0;", stp);
            }
            linef(out, "for (; %s >= 0 ? cvtod(%s) <= %s : cvtod(%s) >= %s; %s = cvnum(cvtod(%s) + %s)) {",
                    stp, var, lim, var, lim, var, var, stp);
            ind++;
            loop_depth++;
            block_gen(out, n->u.forn.body, func_body);
            loop_depth--;
            ind--;
            linef(out, "}");
            ind--;
            linef(out, "}");
            scope_pop();
            break;
        }
        case N_RETURN: {
            write_indent(out);
            buf_put(out, "return ");
            if (n->u.ret.value) {
                int rc;
                char *r = expr_c(n->u.ret.value, &rc);
                if (cur_rettype) {
                    if (rc == CL_CVAL) {
                        if (class_of_ctype(cur_rettype) == CL_PTR)
                            fail(n->line, "cannot return a dynamic value from a function returning '%s'", cur_rettype);
                        buf_putf(out, "(%s)cvtod(%s)", cur_rettype, r);
                    } else {
                        buf_put(out, r);
                    }
                } else {
                    char *w = as_cval(r, rc);
                    buf_put(out, w);
                    free(w);
                }
                free(r);
            } else {
                if (cur_rettype) buf_put(out, "0");
                else buf_put(out, "cvnil()");
            }
            buf_put(out, ";");
            write_endline(out);
            break;
        }
        case N_BREAK:
            if (loop_depth == 0) fail(n->line, "break outside a loop");
            linef(out, "break;");
            break;
        case N_CONTINUE:
            if (loop_depth == 0) fail(n->line, "continue outside a loop");
            linef(out, "continue;");
            break;
        case N_FUNCDEF:
            break; /* hoisted to top level */
        default:
            fail(n->line, "internal: bad statement node");
    }
}

static void param_defs(Buf *out, Vec *params) {
    for (int i = 0; i < params->n; i++) {
        Param *pm = params->d[i];
        char cname[64];
        snprintf(cname, sizeof cname, "%s_%d", pm->name, cg_counter++);
        scope_declare(pm->name, cname, pm->typ);
        if (i) buf_put(out, ", ");
        if (pm->typ) buf_putf(out, "%s %s", pm->typ, cname);
        else buf_putf(out, "cval %s", cname);
    }
}

static char *render(Vec *funcs, Vec *top) {
    Buf out = {NULL, 0, 0};
    cg_counter = 0;
    loop_depth = 0;
    ind = 0;
    sym_table = new_vec();
    scope_mark = NULL;
    scope_top = -1;
    g_funcs = funcs;

    for (int i = 0; i < funcs->n; i++) {
        FuncDef *f = funcs->d[i];
        Buf pl = {NULL, 0, 0};
        char *mf = mangle(f->name);
        scope_push();
        param_defs(&pl, f->params);
        linef(&out, "%s %s(%s);", f->rettype ? f->rettype : "cval", mf, pl.s ? pl.s : "");
        scope_pop();
        free(mf);
        free(pl.s);
    }
    buf_put(&out, "\n");

    for (int i = 0; i < funcs->n; i++) {
        FuncDef *f = funcs->d[i];
        Buf pl = {NULL, 0, 0};
        char *mf = mangle(f->name);
        scope_push();
        param_defs(&pl, f->params);
        linef(&out, "%s %s(%s) {", f->rettype ? f->rettype : "cval", mf, pl.s ? pl.s : "");
        free(mf);
        free(pl.s);
        ind++;
        cur_rettype = f->rettype;
        block_gen(&out, f->body, 1);
        cur_rettype = NULL;
        linef(&out, f->rettype ? "return 0;" : "return cvnil();");
        ind--;
        linef(&out, "}");
        buf_put(&out, "\n");
        scope_pop();
    }

    linef(&out, "int main(void) {");
    scope_push();
    ind++;
    block_gen(&out, top, 0);
    linef(&out, "return 0;");
    ind--;
    linef(&out, "}");
    return out.s;
}

/* ------------------------------- main / CLI ------------------------------- */

static void usage(void) {
    printf("usage: coolc <source.ccpl> [-o out.exe] [--run] [--fast] [--show-c]\n");
}

/* spawn a process directly; no cmd.exe involved. returns its exit code. */
static int run_process(const char *cmdline) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char *buf = xstrdup(cmdline);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessA(NULL, buf, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        free(buf);
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    free(buf);
    return (int)code;
}

int main(int argc, char **argv) {
    const char *src_path = NULL;
    const char *out_path = NULL;
    int do_run = 0, do_show = 0, do_fast = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
            if (i + 1 >= argc) { usage(); return 2; }
            out_path = argv[++i];
        } else if (!strcmp(argv[i], "--run")) {
            do_run = 1;
        } else if (!strcmp(argv[i], "--show-c")) {
            do_show = 1;
        } else if (!strcmp(argv[i], "--fast")) {
            do_fast = 1;
        } else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
            printf("coolc %s - Cool Compilable Programming Language compiler\n", COOLC_VERSION);
            return 0;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage();
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "coolc: unknown option '%s'\n", argv[i]);
            usage();
            return 2;
        } else {
            src_path = argv[i];
        }
    }

    if (!src_path) { usage(); return 2; }

    long len = 0;
    char *src = read_file_bytes(src_path, &len);
    if (!src) { fprintf(stderr, "coolc: cannot read '%s'\n", src_path); return 1; }

    Lexer lx = {0};
    lex(&lx, src);
    Parser p = {lx.tok, lx.ntok, 0};
    Vec *top = expand_gets(parse_file(&p), dir_of(canon_abs(src_path)));
    Vec *funcs = new_vec();
    collect_walk(funcs, top);
    char *c = render(funcs, top);

    if (do_show) {
        fputs(c, stdout);
        return 0;
    }

    char exe_path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { fprintf(stderr, "coolc: cannot locate executable\n"); return 1; }
    char *slash = strrchr(exe_path, '\\');
    if (slash) *slash = 0;
    else exe_path[0] = 0;

    char out[1024];
    if (out_path) {
        snprintf(out, sizeof out, "%s", out_path);
    } else {
        const char *dot = strrchr(src_path, '.');
        size_t base_len = dot && dot > src_path ? (size_t)(dot - src_path) : strlen(src_path);
        snprintf(out, sizeof out, "%.*s.exe", (int)base_len, src_path);
    }

    char tmp_dir[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tmp_dir) == 0 || strlen(tmp_dir) >= MAX_PATH)
        snprintf(tmp_dir, sizeof tmp_dir, ".\\");
    char gen_path[1024];
    snprintf(gen_path, sizeof gen_path, "%sccpl_build_%lu.c", tmp_dir, GetCurrentProcessId());
    FILE *gf = fopen(gen_path, "wb");
    if (!gf) { fprintf(stderr, "coolc: cannot write '%s'\n", gen_path); return 1; }
    fwrite(c, 1, strlen(c), gf);
    fclose(gf);

    char rt_path[1024];
    snprintf(rt_path, sizeof rt_path, "%s\\runtime.h", exe_path);

    char cmd[4096];
    if (do_fast)
        snprintf(cmd, sizeof cmd, "\"%s\\tcc\\tcc.exe\" -O2 -s -include \"%s\" \"%s\" -o \"%s\"",
                 exe_path, rt_path, gen_path, out);
    else
        snprintf(cmd, sizeof cmd, "\"%s\\tcc\\tcc.exe\" -include \"%s\" \"%s\" -o \"%s\"",
                 exe_path, rt_path, gen_path, out);
    int r = run_process(cmd);
    remove(gen_path);
    if (r != 0) {
        fprintf(stderr, "coolc: C compile step failed (%d)\n", r);
        return r;
    }

    printf("built %s\n", out);

    if (do_run) {
        char runcmd[2048];
        snprintf(runcmd, sizeof runcmd, "\"%s\"", out);
        return run_process(runcmd);
    }

    return 0;
}