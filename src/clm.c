/*
 * clm - Cool Library Manager
 *
 * A pip-like package manager for CCPL.
 *
 *   clm login                  link this machine to a GitHub account (PKCE OAuth,
 *                              localhost callback, token kept only in ~\.clm)
 *   clm logout                 forget the linked account
 *   clm whoami                 show the linked account
 *   clm search <query>         search the registry
 *   clm install <name>[@ver]   install a package (and its deps)
 *   clm update [<name>]        update package(s) to the latest version
 *   clm remove <name>          uninstall a package
 *   clm list                   list installed packages
 *   clm publish <dir>          validate + package a library for submission
 *
 * The registry is a static JSON index + zips served over HTTP(S) or file://
 * (e.g. https://cdn.jsdelivr.net/gh/ccpl-lang/ccpl-packages@main). Override
 * with the CLM_REGISTRY environment variable.
 *
 * Dependencies: Windows 10+ only. Uses the bundled curl.exe for HTTP, tar
 * (bsdtar) for zip, and Winsock for the localhost OAuth callback. No external
 * libraries.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#ifndef __TINYC__
#include <winsock2.h>
#include <shellapi.h>
#else
/* TinyCC bundles neither winsock2.h nor shellapi.h. Declare the small slice
 * of Winsock(2) + ShellExecuteA we need, matching the 64-bit ABI. */
typedef unsigned long long SOCKET;
typedef unsigned short u_short;
typedef struct in_addr { unsigned long s_addr; } IN_ADDR, *PIN_ADDR, *LPIN_ADDR;
typedef struct sockaddr_in {
    u_short sin_family;
    u_short sin_port;
    IN_ADDR sin_addr;
    char sin_zero[8];
} SOCKADDR_IN, *PSOCKADDR_IN, *LPSOCKADDR_IN;
struct sockaddr { u_short sa_family; char sa_data[14]; };
struct timeval { long tv_sec; long tv_usec; };
#define _TIMEVAL_DEFINED
typedef struct fd_set {
    unsigned int fd_count;
    SOCKET fd_array[64];
} FD_SET;
#define FD_SETSIZE 64
#define FD_ZERO(s) do { (s)->fd_count = 0; } while (0)
#define FD_SET(fd, s) do { if ((s)->fd_count < FD_SETSIZE) (s)->fd_array[(s)->fd_count++] = (fd); } while (0)

#define AF_INET 2
#define SOCK_STREAM 1
#define IPPROTO_TCP 6
#define SOL_SOCKET 0xffff
#define SO_REUSEADDR 0x0004
#define INVALID_SOCKET ((SOCKET)~(SOCKET)0)
#define SOCKET_ERROR (-1)

typedef unsigned int u_int;
typedef char WSADATA[512];

int  __stdcall WSAStartup(unsigned short wVersionRequested, void *lpWSAData);
int  __stdcall WSACleanup(void);
SOCKET __stdcall socket(int af, int type, int protocol);
int  __stdcall bind(SOCKET s, const struct sockaddr *name, int namelen);
int  __stdcall listen(SOCKET s, int backlog);
int  __stdcall select(int nfds, FD_SET *readfds, FD_SET *writefds, FD_SET *exceptfds, const struct timeval *timeout);
SOCKET __stdcall accept(SOCKET s, struct sockaddr *addr, int *addrlen);
int  __stdcall send(SOCKET s, const char *buf, int len, int flags);
int  __stdcall recv(SOCKET s, char *buf, int len, int flags);
int  __stdcall closesocket(SOCKET s);
unsigned long __stdcall inet_addr(const char *cp);
unsigned short __stdcall htons(unsigned short hostshort);
int  __stdcall setsockopt(SOCKET s, int level, int optname, const char *optval, int optlen);

__declspec(dllimport) HINSTANCE __stdcall ShellExecuteA(void *hwnd, const char *lpOperation,
    const char *lpFile, const char *lpParameters, const char *lpDirectory, int nShowCmd);
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* GitHub OAuth app credentials. The client_id is public by design. The
 * client_secret is NEVER shipped; PKCE ("code_challenge") makes the exchange
 * secure without it. Both may be overridden via the environment. */
#ifndef CLM_CLIENT_ID
#define CLM_CLIENT_ID "Iv23liYDAsixbxCt9ZCg"
#endif

#define CLM_VERSION "0.1.0"
#define REDIRECT_URI "http://127.0.0.1:11894/callback"
#define CALLBACK_PORT 11894
#define DEFAULT_REGISTRY "https://cdn.jsdelivr.net/gh/ccpl-lang/ccpl-packages@main"

/* ------------------------------------------------------------------ */
/* growable buffer / string helpers                                    */
/* ------------------------------------------------------------------ */

typedef struct { char *s; size_t len, cap; } Buf;

static void buf_req(Buf *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    size_t ncap = b->cap ? b->cap * 2 : 256;
    while (ncap < b->len + extra + 1) ncap *= 2;
    char *ns = (char *)realloc(b->s, ncap);
    if (!ns) { fprintf(stderr, "oom\n"); exit(1); }
    b->s = ns; b->cap = ncap;
}
static void buf_put(Buf *b, const char *s) { size_t n = strlen(s); buf_req(b, n); memcpy(b->s + b->len, s, n); b->len += n; b->s[b->len] = 0; }
static void buf_putc(Buf *b, char c) { buf_req(b, 1); b->s[b->len++] = c; b->s[b->len] = 0; }

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = (char *)malloc(n);
    if (!r) { fprintf(stderr, "oom\n"); exit(1); }
    memcpy(r, s, n);
    return r;
}

static char *read_all(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *p = (char *)malloc((size_t)n + 1);
    if (!p) { fclose(f); return NULL; }
    size_t got = fread(p, 1, (size_t)n, f);
    fclose(f);
    p[got] = 0;
    if (out_len) *out_len = got;
    return p;
}

static int write_all(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return w == len;
}

static void mkdirs(const char *path) {
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof tmp, "%s", path);
    size_t n = strlen(tmp);
    while (n && (tmp[n-1] == '\\' || tmp[n-1] == '/')) tmp[--n] = 0;
    if (!n) return;
    for (size_t i = 0; i < n; i++) if (tmp[i] == '/') tmp[i] = '\\';
    if (tmp[1] == ':') {
        for (char *c = tmp + 2; *c; c++)
            if (*c == '\\') { char save = c[1]; c[1] = 0; CreateDirectoryA(tmp, NULL); c[1] = save; }
    }
    CreateDirectoryA(tmp, NULL);
}

static int path_exists(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES;
}
static int is_dir_p(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static const char *home_dir(void) {
    static char dir[MAX_PATH];
    if (!dir[0]) {
        DWORD n = GetEnvironmentVariableA("USERPROFILE", dir, sizeof dir);
        if (n == 0 || n >= sizeof dir) strcpy(dir, ".");
    }
    return dir;
}

/* directory holding clm.exe (cached) */
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

static const char *config_file(const char *leaf) {
    static char buf[MAX_PATH];
    snprintf(buf, sizeof buf, "%s\\.clm\\%s", home_dir(), leaf);
    return buf;
}

static const char *registry_base(void) {
    const char *v = getenv("CLM_REGISTRY");
    return (v && *v) ? v : DEFAULT_REGISTRY;
}

/* ------------------------------------------------------------------ */
/* command runner                                                      */
/* ------------------------------------------------------------------ */

static int run(const char *cmd) {
    STARTUPINFOA si = {0}; si.cb = sizeof si;
    PROCESS_INFORMATION pi = {0};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    /* rout through cmd.exe so builtins like rmdir work */
    char cmdline[8192];
    snprintf(cmdline, sizeof cmdline, "cmd.exe /c %s", cmd);
    char *lc = xstrdup(cmdline);
    if (!CreateProcessA(NULL, lc, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        free(lc);
        return -1;
    }
    free(lc);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 1;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)ec;
}

static char *run_capture(const char *cmd) {
    char *buf = NULL; size_t cap = 0, len = 0;
    FILE *fp = _popen(cmd, "r");
    if (!fp) return NULL;
    char chunk[8192];
    size_t rd;
    while ((rd = fread(chunk, 1, sizeof chunk, fp)) > 0) {
        if (len + rd + 1 > cap) {
            cap = cap ? cap * 2 : 65536;
            while (cap < len + rd + 1) cap *= 2;
            char *ns = (char *)realloc(buf, cap);
            if (!ns) { free(buf); _pclose(fp); return NULL; }
            buf = ns;
        }
        memcpy(buf + len, chunk, rd);
        len += rd;
    }
    _pclose(fp);
    if (!buf) return xstrdup("");
    buf[len] = 0;
    return (char *)realloc(buf, len + 1);
}

static char *url_encode(const char *s) {
    Buf b = {0};
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            buf_putc(&b, (char)c);
        else {
            char h[4];
            snprintf(h, sizeof h, "%%%02X", c);
            buf_put(&b, h);
        }
    }
    return b.s;
}

/* ------------------------------------------------------------------ */
/* minimal JSON reading                                                */
/* ------------------------------------------------------------------ */

/* find the value of "key": <string> within `j`; returns malloc'd or NULL */
static char *json_str(const char *j, const char *key) {
    size_t kl = strlen(key);
    const char *p = j;
    while ((p = strstr(p, key)) != NULL) {
        if (p != j && p[-1] != '"') { p += kl; continue; }
        const char *q = p + kl;
        while (*q && *q != ':') q++;
        if (!*q) return NULL;
        q++;
        while (*q && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
        if (*q != '"') return NULL;
        q++;
        Buf b = {0};
        while (*q && *q != '"') {
            if (*q == '\\' && q[1]) {
                char e = q[1];
                if (e == 'n') buf_putc(&b, '\n');
                else if (e == 't') buf_putc(&b, '\t');
                else if (e == 'r') buf_putc(&b, '\r');
                else buf_putc(&b, e);
                q += 2;
            } else buf_putc(&b, *q++);
        }
        return b.s;
    }
    return NULL;
}

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* find the value of "key": { ... } within `j`; returns the object block as a
 * malloc'd string (JSON object values need their own reader). */
static char *json_obj_str(const char *j, const char *key) {
    size_t kl = strlen(key);
    const char *p = j;
    while ((p = strstr(p, key)) != NULL) {
        if (p != j && p[-1] != '"') { p += kl; continue; }
        const char *q = p + kl;
        while (*q && *q != ':') q++;
        if (!*q) return NULL;
        q++;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
        if (*q != '{') return NULL;
        int depth = 0;
        const char *e = q;
        for (; *e; e++) {
            if (*e == '{') depth++;
            else if (*e == '}') { depth--; if (depth == 0) { e++; break; } }
        }
        if (depth != 0) return NULL;
        size_t bl = (size_t)(e - q);
        char *out = (char *)malloc(bl + 1);
        if (!out) return NULL;
        memcpy(out, q, bl); out[bl] = 0;
        return out;
    }
    return NULL;
}

typedef struct { char *name, *desc, *latest; char *versions[64]; int nver; } Pkg;
static Pkg *g_pkgs = NULL;
static int g_npkg = 0;

static void parse_pkg_block(const char *o) {
    g_pkgs = (Pkg *)realloc(g_pkgs, ((size_t)g_npkg + 1) * sizeof *g_pkgs);
    Pkg *p = &g_pkgs[g_npkg++];
    memset(p, 0, sizeof *p);
    p->name = json_str(o, "name");
    p->desc = json_str(o, "description");
    p->latest = json_str(o, "latest");
    const char *v = strstr(o, "\"versions\"");
    if (v) {
        v = skip_ws(v + strlen("\"versions\""));
        if (*v == ':') {
            v = skip_ws(v + 1);
            if (*v == '[') {
                v++;
                for (;;) {
                    v = skip_ws(v);
                    if (*v != '"') break;
                    v++;
                    Buf b = {0};
                    while (*v && *v != '"') buf_putc(&b, *v++);
                    if (*v != '"') { free(b.s); break; }
                    v++;
                    if (p->nver < 64) p->versions[p->nver++] = b.s; else free(b.s);
                    v = skip_ws(v);
                    if (*v == ',') { v++; continue; }
                    break;
                }
            }
        }
    }
}

/* parse index.json: array of {name, description, latest, versions:[...]} */
static int parse_index(const char *j) {
    g_pkgs = NULL; g_npkg = 0;
    const char *p = j;
    while (*p) {
        const char *o = strchr(p, '{');
        if (!o) break;
        const char *e = strchr(o + 1, '}');
        if (!e) break;
        if (strstr(o, "\"name\"")) {
            size_t bl = (size_t)(e - o + 1);
            char *block = (char *)malloc(bl + 1);
            memcpy(block, o, bl); block[bl] = 0;
            parse_pkg_block(block);
            free(block);
        }
        p = e + 1;
    }
    return g_npkg > 0;
}

static Pkg *find_pkg(const char *name) {
    for (int i = 0; i < g_npkg; i++)
        if (g_pkgs[i].name && !strcmp(g_pkgs[i].name, name)) return &g_pkgs[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* SHA-256 (public-domain compact implementation) + base64url          */
/* ------------------------------------------------------------------ */

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

typedef struct { uint32_t h[8]; uint64_t bitlen; unsigned char buf[64]; size_t buflen; } Sha256;

static void sha_init(Sha256 *s) {
    static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(s->h, iv, sizeof iv);
    s->bitlen = 0; s->buflen = 0;
}
static void sha_block(Sha256 *s, const unsigned char *buf) {
    uint32_t m[64], a=s->h[0], b=s->h[1], c=s->h[2], d=s->h[3], e=s->h[4], f=s->h[5], g=s->h[6], h=s->h[7];
    int i;
    for (i = 0; i < 16; i++) m[i] = ((uint32_t)buf[i*4]<<24)|((uint32_t)buf[i*4+1]<<16)|((uint32_t)buf[i*4+2]<<8)|buf[i*4+3];
    for (; i < 64; i++) {
        uint32_t s0 = rotr(m[i-15],7)^rotr(m[i-15],18)^(m[i-15]>>3);
        uint32_t s1 = rotr(m[i-2],17)^rotr(m[i-2],19)^(m[i-2]>>10);
        m[i] = m[i-16] + s0 + m[i-7] + s1;
    }
    for (i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e,6)^rotr(e,11)^rotr(e,25);
        uint32_t ch = (e&f)^((~e)&g);
        uint32_t t1 = h + S1 + ch + sha256_k[i] + m[i];
        uint32_t S0 = rotr(a,2)^rotr(a,13)^rotr(a,22);
        uint32_t maj = (a&b)^(a&c)^(b&c);
        uint32_t t2 = S0 + maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    s->h[0]+=a; s->h[1]+=b; s->h[2]+=c; s->h[3]+=d;
    s->h[4]+=e; s->h[5]+=f; s->h[6]+=g; s->h[7]+=h;
}
static void sha_update(Sha256 *s, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; i++) {
        s->buf[s->buflen++] = p[i];
        s->bitlen += 8;
        if (s->buflen == 64) { sha_block(s, s->buf); s->buflen = 0; }
    }
}
static void sha_final(Sha256 *s, unsigned char out[32]) {
    uint64_t bl = s->bitlen;
    unsigned char pad = 0x80;
    sha_update(s, &pad, 1);
    unsigned char zero = 0;
    while (s->buflen != 56) sha_update(s, &zero, 1);
    unsigned char lenb[8];
    for (int i = 0; i < 8; i++) lenb[7-i] = (unsigned char)(bl >> (i*8));
    sha_update(s, lenb, 8);
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (unsigned char)(s->h[i] >> 24);
        out[i*4+1] = (unsigned char)(s->h[i] >> 16);
        out[i*4+2] = (unsigned char)(s->h[i] >> 8);
        out[i*4+3] = (unsigned char)s->h[i];
    }
}

static const char b64t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

/* RFC 4648 base64url without padding (a 32-byte digest becomes 43 chars). */
static void base64url(const unsigned char *in, size_t n, Buf *out) {
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        size_t left = n - i;
        if (left > 1) v |= (uint32_t)in[i + 1] << 8;
        if (left > 2) v |= in[i + 2];
        buf_putc(out, b64t[(v >> 18) & 63]);
        buf_putc(out, b64t[(v >> 12) & 63]);
        if (left > 1) buf_putc(out, b64t[(v >> 6) & 63]);
        if (left > 2) buf_putc(out, b64t[v & 63]);
    }
}

static void pkce_pair(char *verifier, size_t vsz, char *challenge, size_t csz) {
    srand((unsigned)(time(NULL) ^ GetCurrentProcessId()));
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    for (size_t i = 0; i + 1 < vsz; i++) verifier[i] = alphabet[rand() % (sizeof alphabet - 1)];
    verifier[vsz - 1] = 0;
    Sha256 s; sha_init(&s);
    sha_update(&s, verifier, strlen(verifier));
    unsigned char dig[32];
    sha_final(&s, dig);
    Buf b = {0};
    base64url(dig, 32, &b);
    snprintf(challenge, csz, "%s", b.s);
    free(b.s);
}

/* ------------------------------------------------------------------ */
/* auth (auth.json in %USERPROFILE%\.clm)                              */
/* ------------------------------------------------------------------ */

static const char *auth_file(void) { return config_file("auth.json"); }

static int auth_save(const char *client_id, const char *access_token, const char *refresh_token,
                     const char *login, long long expires_in, long long refresh_expires_in) {
    mkdirs(config_file("."));
    char b[4096];
    long long now = (long long)time(NULL);
    snprintf(b, sizeof b,
        "{\n \"client_id\": \"%s\",\n \"access_token\": \"%s\",\n \"refresh_token\": \"%s\",\n"
        " \"login\": \"%s\",\n \"expires_at\": %lld,\n \"refresh_expires_at\": %lld\n}\n",
        client_id, access_token, refresh_token ? refresh_token : "", login,
        expires_in ? now + expires_in : 0, refresh_expires_in ? now + refresh_expires_in : 0);
    return write_all(auth_file(), b, strlen(b));
}

static char *auth_get(const char *key) {
    char *j = read_all(auth_file(), NULL);
    if (!j) return NULL;
    char *v = json_str(j, key);
    free(j);
    return v;
}

static void auth_clear(void) {
    FILE *f = fopen(auth_file(), "wb");
    if (f) { fputs("{}", f); fclose(f); }
}

/* POST an urlencoded form to GitHub's token endpoint, encoding each
 * key=value segment individually so `&` separators survive. */
static char *oauth_post(const char *body) {
    Buf q = {0};
    const char *p = body;
    for (;;) {
        const char *amp = strchr(p, '&');
        size_t len = amp ? (size_t)(amp - p) : strlen(p);
        char *seg = (char *)malloc(len + 1);
        if (!seg) { free(q.s); return NULL; }
        memcpy(seg, p, len); seg[len] = 0;
        char *enc = url_encode(seg);
        buf_put(&q, enc);
        free(enc); free(seg);
        if (!amp) break;
        buf_put(&q, "&");
        p = amp + 1;
    }
    Buf cmd = {0};
    buf_put(&cmd, "curl.exe -sS -X POST https://github.com/login/oauth/access_token"
                  " -H \"Accept: application/json\" -d \"");
    buf_put(&cmd, q.s);
    buf_put(&cmd, "\"");
    char *json = run_capture(cmd.s);
    free(cmd.s); free(q.s);
    return json;
}

static char *api_call(const char *token, const char *url) {
    Buf cmd = {0};
    char *enc = url_encode(token);
    buf_put(&cmd, "curl.exe -sS -H \"Authorization: Bearer ");
    buf_put(&cmd, enc);
    buf_put(&cmd, "\" -H \"Accept: application/json\" ");
    buf_put(&cmd, url);
    char *json = run_capture(cmd.s);
    free(cmd.s); free(enc);
    return json;
}

/* ------------------------------------------------------------------ */
/* HTTP callback server (localhost OAuth)                              */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *client_id;
    const char *client_secret;   /* NULL or empty when PKCE-only */
    const char *verifier;
    const char *expect_state;
    char out_login[256];
    char err[512];
    int code;
} LoginCtx;

static void login_page(char *buf, size_t n, int error, const char *login) {
    if (error) {
        snprintf(buf, n,
            "<!doctype html><html><head><meta charset='utf-8'><title>clm</title></head>"
            "<body style='font-family:system-ui,sans-serif;background:#181a22;color:#e6e6e6;"
            "display:flex;align-items:center;justify-content:center;height:100vh;margin:0'>"
            "<div style='text-align:center'><h1>Oh no.</h1><p>clm could not complete the link.</p></div>"
            "</body></html>");
        return;
    }
    snprintf(buf, n,
        "<!doctype html><html><head><meta charset='utf-8'><title>clm</title></head>"
        "<body style='font-family:system-ui,sans-serif;background:#181a22;color:#e6e6e6;"
        "display:flex;align-items:center;justify-content:center;height:100vh;margin:0'>"
        "<div style='text-align:center'><h1 style='margin:0 0 8px'>Hey, you're all set!</h1>"
        "<p>Your GitHub account <b>%s</b> is now linked to your <b>clm</b>.</p>"
        "<p style='color:#8a8fa3'>You can close this tab and get back to your terminal.</p></div>"
        "</body></html>", login ? login : "you");
}

/* serve the localhost callback; on success performs the token exchange inside
 * the connection and returns ctx->code == 0 with ctx->out_login filled. */
static int http_server_login(LoginCtx *ctx, int timeout_sec) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return -1;
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) { WSACleanup(); return -1; }
    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = inet_addr("127.0.0.1");
    a.sin_port = htons((unsigned short)CALLBACK_PORT);
    if (bind(ls, (struct sockaddr *)&a, sizeof a) != 0) { closesocket(ls); WSACleanup(); return -2; }
    if (listen(ls, 4) != 0) { closesocket(ls); WSACleanup(); return -3; }

    int result = -4;
    for (int attempt = 0; attempt < timeout_sec * 2; attempt++) {
        struct fd_set fs; FD_ZERO(&fs); FD_SET(ls, &fs);
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 500000;
        int sel = select(0, &fs, NULL, NULL, &tv);
        if (sel <= 0) continue;
        struct sockaddr_in ca;
        int csz = sizeof ca;
        SOCKET cs = accept(ls, (struct sockaddr *)&ca, &csz);
        if (cs == INVALID_SOCKET) continue;
        char req[8192];
        int rn = 0;
        while (rn < (int)sizeof req - 1) {
            int n = recv(cs, req + rn, sizeof req - 1 - (size_t)rn, 0);
            if (n <= 0) break;
            rn += n;
            if (req[rn-1] == '\n') break;
        }
        req[rn] = 0;

        char path[1024] = "";
        if (rn >= 4 && strncmp(req, "GET ", 4) == 0) {
            char *sp = strchr(req + 4, ' ');
            if (sp) { size_t hl = (size_t)(sp - (req + 4)); if (hl < sizeof path) { memcpy(path, req + 4, hl); path[hl] = 0; } }
        }

        int handled = 0;
        if (strncmp(path, "/callback", 9) == 0 && ctx->code == 0) {
            char code[512] = ""; char state[128] = "";
            const char *q = strstr(path, "?");
            if (q) {
                const char *c = strstr(q, "code=");
                if (c) { c += 5; int i = 0; while (*c && *c != '&' && i < 511) code[i++] = *c++; code[i] = 0; }
                const char *s = strstr(q, "state=");
                if (s) { s += 6; int i = 0; while (*s && *s != '&' && i < 127) state[i++] = *s++; state[i] = 0; }
            }
            if (ctx->expect_state && *ctx->expect_state && state[0] && strcmp(state, ctx->expect_state)) {
                ctx->code = 99;
                snprintf(ctx->err, sizeof ctx->err, "state mismatch");
            } else if (!code[0]) {
                ctx->code = 99;
                snprintf(ctx->err, sizeof ctx->err, "no code in callback");
            } else {
                Buf body = {0};
                buf_put(&body, "client_id="); buf_put(&body, ctx->client_id);
                if (ctx->client_secret && *ctx->client_secret) { buf_put(&body, "&client_secret="); buf_put(&body, ctx->client_secret); }
                buf_put(&body, "&code="); buf_put(&body, code);
                buf_put(&body, "&code_verifier="); buf_put(&body, ctx->verifier);
                buf_put(&body, "&redirect_uri="); buf_put(&body, REDIRECT_URI);
                char *json = oauth_post(body.s);
                free(body.s);
                char *at = NULL, *rt = NULL;
                long long e1 = 0, e2 = 0;
                if (json) {
                    at = json_str(json, "access_token");
                    rt = json_str(json, "refresh_token");
                    char *ei = json_str(json, "expires_in"); if (ei) { e1 = atoll(ei); free(ei); }
                    char *rei = json_str(json, "refresh_token_expires_in"); if (rei) { e2 = atoll(rei); free(rei); }
                    char *err = json_str(json, "error_description");
                    if (err && !at) { snprintf(ctx->err, sizeof ctx->err, "%s", err); free(err); }
                    free(json);
                }
                if (!at) {
                    ctx->code = 98;
                    snprintf(ctx->err, sizeof ctx->err, "could not exchange the authorization code");
                } else {
                    char *me = api_call(at, "https://api.github.com/user");
                    char *login = NULL;
                    if (me) { login = json_str(me, "login"); free(me); }
                    if (!login) login = xstrdup("you");
                    snprintf(ctx->out_login, sizeof ctx->out_login, "%s", login);
                    ctx->code = 0;
                    char page[4096];
                    login_page(page, sizeof page, 0, login);
                    char resp[4608];
                    snprintf(resp, sizeof resp,
                        "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\n\r\n%s",
                        (int)strlen(page), page);
                    send(cs, resp, (int)strlen(resp), 0);
                    closesocket(cs);
                    free(login); free(at); free(rt);
                    result = 0;
                    break;
                }
                free(at); free(rt);
            }
            handled = 1;
        }
        if (!handled) {
            const char *page = "<!doctype html><html><body><p style='font-family:sans-serif'>clm - waiting for authorization...</p></body></html>";
            char resp[4096];
            snprintf(resp, sizeof resp,
                "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\n\r\n%s",
                (int)strlen(page), page);
            send(cs, resp, (int)strlen(resp), 0);
            closesocket(cs);
        }
    }
    closesocket(ls);
    WSACleanup();
    return result;
}

static void open_browser(const char *url) {
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

/* ------------------------------------------------------------------ */
/* commands                                                            */
/* ------------------------------------------------------------------ */

static void help(void) {
    printf(
        "clm %s - Cool Library Manager for CCPL\n\n"
        "usage:\n"
        "  clm login                     link this machine to a GitHub account (browser OAuth)\n"
        "  clm logout                    forget the linked account\n"
        "  clm whoami                    show the linked account\n"
        "  clm search <query>            search the package registry\n"
        "  clm install <name>[@<ver>]    install a package and its dependencies\n"
        "  clm update [<name>]           update installed package(s)\n"
        "  clm remove <name>             uninstall a package\n"
        "  clm list                      list installed packages\n"
        "  clm publish <dir>             build a library for publishing\n"
        "  clm -h | --help               this help\n"
        "  clm --version                 version\n\n"
        "registry: %s\n",
        CLM_VERSION, registry_base());
}

static int cmd_login(void) {
    char verifier[64], challenge[128];
    pkce_pair(verifier, sizeof verifier, challenge, sizeof challenge);

    char state[33];
    srand((unsigned)(time(NULL) ^ GetCurrentProcessId()));
    for (int i = 0; i < 32; i++) state[i] = (char)("abcdef0123456789"[rand() % 16]);
    state[32] = 0;

    const char *cid = getenv("CLM_CLIENT_ID");
    if (!cid || !*cid) cid = CLM_CLIENT_ID;
    const char *secret = getenv("CLM_CLIENT_SECRET");

    Buf url = {0};
    buf_put(&url, "https://github.com/login/oauth/authorize?response_type=code&client_id=");
    buf_put(&url, cid);
    buf_put(&url, "&redirect_uri=");
    buf_put(&url, url_encode(REDIRECT_URI));
    buf_put(&url, "&state="); buf_put(&url, state);
    buf_put(&url, "&code_challenge="); buf_put(&url, challenge);
    buf_put(&url, "&code_challenge_method=S256");

    printf("Opening your browser to authorize clm ...\n");
    printf("If nothing opens, visit:\n  %s\n\n", url.s);
    open_browser(url.s);
    free(url.s);

    LoginCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.client_id = cid;
    ctx.client_secret = secret;
    ctx.verifier = verifier;
    ctx.expect_state = state;

    printf("Waiting for GitHub to link this machine (timeout 180 s)...\n");
    int r = http_server_login(&ctx, 180);
    if (r == -2 || r == -3) {
        fprintf(stderr, "cannot listen on %s — the port is busy or the GitHub App\n", REDIRECT_URI);
        fprintf(stderr, "callback URL does not match exactly.\n");
        return 1;
    }
    if (r != 0) { fprintf(stderr, "login cancelled or timed out.\n"); return 1; }
    if (ctx.code == 0 && ctx.out_login[0]) {
        printf("Linked! Your account %s is now connected to clm.\n", ctx.out_login);
        printf("You can close the browser tab and use `clm install <name>`.\n");
        return 0;
    }
    fprintf(stderr, "login failed: %s\n", ctx.err[0] ? ctx.err : "unknown error");
    return 1;
}

static int cmd_whoami(void) {
    char *at = auth_get("access_token");
    char *rt = auth_get("refresh_token");
    if (!at && rt) {
        char *cid = auth_get("client_id");
        char *nat = NULL, *nrt = NULL; long long e1 = 0, e2 = 0;
        Buf body = {0};
        buf_put(&body, "grant_type=refresh_token&client_id=");
        buf_put(&body, cid ? cid : CLM_CLIENT_ID);
        buf_put(&body, "&refresh_token="); buf_put(&body, rt);
        char *json = oauth_post(body.s);
        free(body.s);
        if (json) {
            nat = json_str(json, "access_token");
            nrt = json_str(json, "refresh_token");
            char *ei = json_str(json, "expires_in"); if (ei) { e1 = atoll(ei); free(ei); }
            char *rei = json_str(json, "refresh_token_expires_in"); if (rei) { e2 = atoll(rei); free(rei); }
            free(json);
        }
        if (nat) {
            char *login = auth_get("login");
            auth_save(cid ? cid : CLM_CLIENT_ID, nat, nrt ? nrt : "", login ? login : "", e1, e2);
            free(login);
            free(at); free(rt);
            at = nat; rt = nrt;
        } else free(nat);
        free(cid);
    }
    if (!at) { printf("not logged in. run `clm login`.\n"); return 0; }
    char *login = auth_get("login");
    if (login && *login) { printf("%s\n", login); }
    else {
        char *me = api_call(at, "https://api.github.com/user");
        if (me) {
            login = json_str(me, "login");
            if (login) printf("%s\n", login);
            else fprintf(stderr, "could not resolve the linked account.\n");
            free(me);
        } else fprintf(stderr, "could not reach GitHub.\n");
    }
    free(at); free(rt); free(login);
    return 0;
}

static int cmd_logout(void) {
    auth_clear();
    printf("logged out.\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* registry access                                                     */
/* ------------------------------------------------------------------ */

static char *registry_index(void) {
    Buf cmd = {0};
    buf_put(&cmd, "curl.exe -sS -fL \"");
    buf_put(&cmd, registry_base());
    buf_put(&cmd, "/index.json\"");
    char *json = run_capture(cmd.s);
    free(cmd.s);
    return json;
}

static int fetch_url_to(const char *url, const char *dest) {
    Buf cmd = {0};
    buf_put(&cmd, "curl.exe -sS -fL -o \"");
    buf_put(&cmd, dest);
    buf_put(&cmd, "\" \"");
    buf_put(&cmd, url);
    buf_put(&cmd, "\"");
    int rc = run(cmd.s);
    free(cmd.s);
    return rc != 0;
}

static int cmd_search(int argc, char **argv) {
    if (argc < 1 || !*argv[0]) { fprintf(stderr, "usage: clm search <query>\n"); return 1; }
    char *j = registry_index();
    if (!j || !*j) { fprintf(stderr, "could not fetch the registry (%s).\n", registry_base()); free(j); return 1; }
    parse_index(j);
    free(j);
    int shown = 0;
    for (int i = 0; i < g_npkg; i++) {
        Pkg *o = &g_pkgs[i];
        if (!o->name) continue;
        const char *q = argv[0];
        if (q[0] && !strstr(o->name, q) && !(o->desc && strstr(o->desc, q))) continue;
        printf("%-20s %-10s %s\n", o->name, o->latest ? o->latest : "?", o->desc ? o->desc : "");
        shown++;
    }
    if (!shown) printf("no packages match.\n");
    return 0;
}

static int ver_satisfies(const char *v, const char *spec) {
    if (!spec || !*spec) return 1;
    if (spec[0] == '^') {
        double vd = atof(v), pd = atof(spec + 1);
        /* same major */
        if ((long long)vd != (long long)pd) return 0;
        return vd >= pd;
    }
    if (spec[0] == '>') {
        double vd = atof(v);
        if (spec[1] == '=') return vd >= atof(spec + 2);
        return vd > atof(spec + 1);
    }
    if (spec[0] == '<') {
        double vd = atof(v);
        if (spec[1] == '=') return vd <= atof(spec + 2);
        return vd < atof(spec + 1);
    }
    if (spec[0] == '~') return atof(v) >= atof(spec + 1) && atof(v) < atof(spec + 1) + 1;
    return !strcmp(v, spec);
}

static const char *resolve_version(Pkg *o, const char *spec) {
    if (!spec || !*spec || !strcmp(spec, "latest")) return o->latest;
    const char *best = NULL;
    for (int i = 0; i < o->nver; i++) {
        const char *v = o->versions[i];
        if (v && ver_satisfies(v, spec)) {
            if (!best || atof(v) > atof(best)) best = v;
        }
    }
    return best;
}

static const char *installed_file(void) {
    static char buf[MAX_PATH];
    snprintf(buf, sizeof buf, "%s\\packages\\installed.json", exe_dir());
    return buf;
}

static const char *installed_dir(void) {
    static char buf[MAX_PATH];
    snprintf(buf, sizeof buf, "%s\\packages", exe_dir());
    return buf;
}

typedef struct { char *key; char *val; } InstPair;

/* parse every "key": "value" pair from installed.json into a malloc'd array.
 * Returns the number of pairs; *arr gets the array (free with inst_pairs_free). */
static int inst_pairs(InstPair **arr) {
    InstPair *out = NULL; int n = 0;
    char *j = read_all(installed_file(), NULL);
    if (j) {
        const char *p = j;
        while ((p = strstr(p, "\"")) != NULL) {
            const char *ke = strchr(p + 1, '"');
            if (!ke) break;
            const char *q = skip_ws(ke + 1);
            if (*q != ':') { p = ke + 1; continue; }
            q = skip_ws(q + 1);
            if (*q != '"') { p = ke + 1; continue; }
            const char *ve = strchr(q + 1, '"');
            if (!ve) break;
            size_t kl = (size_t)(ke - p - 1), vl = (size_t)(ve - q - 1);
            InstPair *it = (InstPair *)realloc(out, ((size_t)n + 1) * sizeof *out);
            if (!it) { free(j); break; }
            out = it;
            out[n].key = (char *)malloc(kl + 1); memcpy(out[n].key, p + 1, kl); out[n].key[kl] = 0;
            out[n].val = (char *)malloc(vl + 1); memcpy(out[n].val, q + 1, vl); out[n].val[vl] = 0;
            if (!out[n].key || !out[n].val) break;
            n++;
            p = ve + 1;
        }
        free(j);
    }
    *arr = out;
    return n;
}

static void inst_pairs_free(InstPair *arr, int n) {
    for (int i = 0; i < n; i++) { free(arr[i].key); free(arr[i].val); }
    free(arr);
}

static int installed_set(const char *name, const char *ver, int remove) {
    InstPair *pairs = NULL;
    int n = inst_pairs(&pairs);
    Buf out = {0};
    buf_put(&out, "{\n");
    int wrote = 0;
    for (int i = 0; i < n; i++) {
        const char *key = pairs[i].key, *val = pairs[i].val;
        if (strcmp(key, name)) {
            buf_put(&out, "\t\"");
            buf_put(&out, key);
            buf_put(&out, "\": \"");
            buf_put(&out, val ? val : "");
            buf_put(&out, "\",\n");
            wrote++;
        }
    }
    inst_pairs_free(pairs, n);
    if (!remove && ver) {
        buf_put(&out, "\t\"");
        buf_put(&out, name);
        buf_put(&out, "\": \"");
        buf_put(&out, ver);
        buf_put(&out, "\",\n");
        wrote++;
    }
    if (wrote) out.len -= 2;
    buf_put(&out, "\n}\n");
    int ok = write_all(installed_file(), out.s ? out.s : "{}", out.s ? out.len : 2);
    free(out.s);
    return ok;
}

static int install_pkg(const char *name, const char *spec, int depth);

static int install_one(const char *name, const char *version, int depth) {
    char zipurl[MAX_PATH * 2];
    snprintf(zipurl, sizeof zipurl, "%s/packages/%s/%s-%s.zip", registry_base(), name, name, version);
    char tmpdir[MAX_PATH];
    snprintf(tmpdir, sizeof tmpdir, "%s\\.clm\\tmp", home_dir());
    mkdirs(tmpdir);
    char zip[MAX_PATH], ex[MAX_PATH];
    snprintf(zip, sizeof zip, "%s\\pkg.zip", tmpdir);
    snprintf(ex, sizeof ex, "%s\\x", tmpdir);

    char pkgpath[MAX_PATH];
    snprintf(pkgpath, sizeof pkgpath, "%s\\packages\\%s", exe_dir(), name);
    if (path_exists(pkgpath)) {
        char cmd[MAX_PATH * 2];
        snprintf(cmd, sizeof cmd, "rmdir /s /q \"%s\"", pkgpath);
        run(cmd);
    }

    printf("fetching %s %s ...\n", name, version);
    if (fetch_url_to(zipurl, zip) != 0) {
        fprintf(stderr, "failed to download %s\n", zipurl);
        return 1;
    }
    if (path_exists(ex)) { char cmd[MAX_PATH * 2]; snprintf(cmd, sizeof cmd, "rmdir /s /q \"%s\"", ex); run(cmd); }
    mkdirs(ex);
    char cmd[MAX_PATH * 4];
    snprintf(cmd, sizeof cmd, "tar.exe -xf \"%s\" -C \"%s\"", zip, ex);
    if (run(cmd) != 0) { fprintf(stderr, "failed to unpack %s\n", zip); return 1; }

    char src[MAX_PATH];
    snprintf(src, sizeof src, "%s\\%s", ex, name);
    if (!is_dir_p(src)) {
        WIN32_FIND_DATAA fd;
        char pat[MAX_PATH];
        snprintf(pat, sizeof pat, "%s\\*", ex);
        HANDLE h = FindFirstFileA(pat, &fd);
        int found = 0;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, "..")) {
                    snprintf(src, sizeof src, "%s\\%s", ex, fd.cFileName);
                    found = 1;
                    break;
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        if (!found) { fprintf(stderr, "package %s has no contents.\n", name); return 1; }
    }
    mkdirs(installed_dir());
    if (MoveFileA(src, pkgpath) == 0) {
        snprintf(cmd, sizeof cmd, "xcopy \"%s\" \"%s\" /e /i /q /h /y", src, pkgpath);
        run(cmd);
    }
    installed_set(name, version, 0);

    /* dependencies from clm.json */
    char manifest[MAX_PATH];
    snprintf(manifest, sizeof manifest, "%s\\clm.json", pkgpath);
    char *mj = read_all(manifest, NULL);
    if (mj) {
        char *deps = json_obj_str(mj, "deps");
        if (deps) {
            const char *p = deps;
            while ((p = strstr(p, "\"")) != NULL) {
                const char *ke = strchr(p + 1, '"');
                if (!ke) break;
                const char *q2 = skip_ws(ke + 1);
                if (*q2 != ':') { p = ke + 1; continue; }
                q2 = skip_ws(q2 + 1);
                if (*q2 != '"') { p = ke + 1; continue; }
                const char *ve = strchr(q2 + 1, '"');
                if (!ve) break;
                size_t kl = (size_t)(ke - p - 1), vl = (size_t)(ve - q2 - 1);
                char *dn = (char *)malloc(kl + 1);
                memcpy(dn, p + 1, kl); dn[kl] = 0;
                char *dv = (char *)malloc(vl + 1);
                memcpy(dv, q2 + 1, vl); dv[vl] = 0;
                if (kl && vl) install_pkg(dn, dv, depth + 1);
                free(dn); free(dv);
                p = ve + 1;
            }
            free(deps);
        }
        free(mj);
    }

    printf("installed %s %s\n", name, version);
    return 0;
}

static int install_pkg(const char *name, const char *spec, int depth) {
    if (depth > 8) { fprintf(stderr, "dependency depth exceeded at %s\n", name); return 1; }
    char *idx = registry_index();
    if (!idx || !*idx) { fprintf(stderr, "could not fetch the registry (%s).\n", registry_base()); free(idx); return 1; }
    parse_index(idx);
    free(idx);
    Pkg *o = find_pkg(name);
    if (!o) { fprintf(stderr, "package not found: %s\n", name); return 1; }
    if (!spec || !*spec) spec = o->latest;

    char *j = read_all(installed_file(), NULL);
    if (j) {
        char *cur = json_str(j, name);
        if (cur) {
            if (ver_satisfies(cur, spec)) {
                printf("%s already installed (%s)\n", name, cur);
                free(cur); free(j);
                return 0;
            }
            free(cur);
        }
        free(j);
    }
    const char *ver = resolve_version(o, spec);
    if (!ver) { fprintf(stderr, "no version of %s satisfies '%s'\n", name, spec); return 1; }
    return install_one(name, ver, depth);
}

static int cmd_install(int argc, char **argv) {
    if (argc < 1 || !*argv[0]) { fprintf(stderr, "usage: clm install <name>[@<version>]\n"); return 1; }
    char nb[256];
    snprintf(nb, sizeof nb, "%s", argv[0]);
    char *name = nb;
    const char *spec = NULL;
    char *at = strchr(nb, '@');
    if (at) { *at = 0; spec = at + 1; }
    if (!*name) { fprintf(stderr, "usage: clm install <name>[@<version>]\n"); return 1; }
    return install_pkg(name, spec, 0);
}

static int cmd_list(void) {
    InstPair *pairs = NULL;
    int n = inst_pairs(&pairs);
    if (!n) { printf("no packages installed. run `clm install <name>`\n"); return 0; }
    for (int i = 0; i < n; i++) printf("%-20s %s\n", pairs[i].key, pairs[i].val);
    inst_pairs_free(pairs, n);
    return 0;
}

static int cmd_remove(int argc, char **argv) {
    if (argc < 1 || !*argv[0]) { fprintf(stderr, "usage: clm remove <name>\n"); return 1; }
    char pkgpath[MAX_PATH];
    snprintf(pkgpath, sizeof pkgpath, "%s\\packages\\%s", exe_dir(), argv[0]);
    if (!path_exists(pkgpath)) { fprintf(stderr, "package %s is not installed.\n", argv[0]); return 1; }
    char c[MAX_PATH * 2];
    snprintf(c, sizeof c, "rmdir /s /q \"%s\"", pkgpath);
    if (run(c) != 0) { fprintf(stderr, "could not remove %s\n", argv[0]); return 1; }
    installed_set(argv[0], NULL, 1);
    printf("removed %s\n", argv[0]);
    return 0;
}

static int cmd_update(int argc, char **argv) {
    if (argc >= 1 && *argv[0]) {
        InstPair *pairs = NULL;
        int n = inst_pairs(&pairs);
        for (int i = 0; i < n; i++) {
            if (!strcmp(pairs[i].key, argv[0])) { printf("checking %s ...\n", argv[0]); install_pkg(argv[0], NULL, 0); inst_pairs_free(pairs, n); return 0; }
        }
        inst_pairs_free(pairs, n);
        fprintf(stderr, "package %s is not installed.\n", argv[0]);
        return 1;
    }
    InstPair *pairs = NULL;
    int n = inst_pairs(&pairs);
    if (!n) { printf("no packages installed.\n"); return 0; }
    for (int i = 0; i < n; i++) {
        printf("checking %s ...\n", pairs[i].key);
        install_pkg(pairs[i].key, NULL, 0);
    }
    inst_pairs_free(pairs, n);
    return 0;
}

static int cmd_publish(int argc, char **argv) {
    if (argc < 1 || !*argv[0]) { fprintf(stderr, "usage: clm publish <dir>\n"); return 1; }
    const char *dir = argv[0];
    char manifest[MAX_PATH];
    snprintf(manifest, sizeof manifest, "%s\\clm.json", dir);
    char *mj = read_all(manifest, NULL);
    if (!mj) { fprintf(stderr, "no clm.json found in %s\n", dir); return 1; }
    char *name = json_str(mj, "name");
    char *ver = json_str(mj, "version");
    if (!name || !ver || !*name || !*ver) {
        fprintf(stderr, "clm.json must have \"name\" and \"version\".\n");
        free(mj); free(name); free(ver);
        return 1;
    }
    printf("building %s-%s ...\n", name, ver);
    char tmpdir[MAX_PATH];
    snprintf(tmpdir, sizeof tmpdir, "%s\\.clm\\pub", home_dir());
    if (path_exists(tmpdir)) { char c[MAX_PATH]; snprintf(c, sizeof c, "rmdir /s /q \"%s\"", tmpdir); run(c); }
    mkdirs(tmpdir);
    char stage[MAX_PATH];
    snprintf(stage, sizeof stage, "%s\\%s", tmpdir, name);
    char cmd[MAX_PATH * 2];
    snprintf(cmd, sizeof cmd, "xcopy \"%s\" \"%s\" /e /i /q /h /y", dir, stage);
    if (run(cmd) != 0) { fprintf(stderr, "could not stage %s\n", dir); free(mj); free(name); free(ver); return 1; }
    char out[MAX_PATH];
    snprintf(out, sizeof out, "%s-%s.zip", name, ver);
    snprintf(cmd, sizeof cmd, "tar.exe -a -c -f \"%s\" -C \"%s\" \"%s\"", out, tmpdir, name);
    int ok = run(cmd) == 0;
    if (ok) {
        printf("built %s\n", out);
        printf("submit it to the registry: open a PR on\n");
        printf("  https://github.com/ccpl-lang/ccpl-packages\n");
        printf("with packages/%s/%s and an updated index.json.\n", name, out);
    } else {
        fprintf(stderr, "zip creation failed.\n");
    }
    free(mj); free(name); free(ver);
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) { help(); return 0; }
    if (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-v")) { printf("clm %s\n", CLM_VERSION); return 0; }
    const char *cmd = argv[1];
    int a = argc - 2;
    char **av = argv + 2;
    if (!strcmp(cmd, "login")) return cmd_login();
    if (!strcmp(cmd, "logout")) return cmd_logout();
    if (!strcmp(cmd, "whoami")) return cmd_whoami();
    if (!strcmp(cmd, "search")) return cmd_search(a, av);
    if (!strcmp(cmd, "install")) return cmd_install(a, av);
    if (!strcmp(cmd, "update")) return cmd_update(a, av);
    if (!strcmp(cmd, "remove")) return cmd_remove(a, av);
    if (!strcmp(cmd, "list")) return cmd_list();
    if (!strcmp(cmd, "publish")) return cmd_publish(a, av);
    fprintf(stderr, "unknown command: %s\nrun `clm -h` for help.\n", cmd);
    return 1;
}