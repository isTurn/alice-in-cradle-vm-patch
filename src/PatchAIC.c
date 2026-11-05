/* ============================================================================
 *  PatchAIC  -  Alice in Cradle VM / emulator detection remover
 *
 *  Native Win32 replacement for the old dnlib-based PatchTool.
 *  Zero dependency: statically linked C, only imports KERNEL32.dll.
 *  Runs on Windows and on Wine / box64 (Winlator, Android) with no .NET
 *  runtime and no PowerShell.
 *
 *  What it does
 *  ------------
 *  The game (v0.29+) inspects the environment inside
 *  unsafeAssem.dll -> XX.IN::Awake() : it looks at the graphics adapter
 *  name (virgl / turnip / vortex / gamefusion) and at emulator marker paths,
 *  and on a hit assigns `flag = false`, which then loads
 *  SceneCannotLaunch_InvalidEnviroment and refuses to start.
 *
 *  This tool locates that single `ldc.i4.0 ; stloc.0 ; ldloc.0` inside
 *  XX.IN::Awake() and rewrites the first two instructions as `nop`
 *  (1:1 byte replacement - no metadata moved, no RVA changed, file size
 *  identical). The detection still runs, its result is just discarded.
 *
 *  Deliberately NOT touched:
 *    - the exe SHA1 / version integrity check (only the DLL is modified)
 *    - checkLocalFileAvailable() data-file verification
 *
 *  Usage
 *  -----
 *    PatchAIC.exe [dll-or-folder]        patch in place (auto-discover if omitted)
 *    PatchAIC.exe <in.dll> <out.dll>     patch into a separate output file
 *    PatchAIC.exe --check  [dll]         report status only, change nothing
 *    PatchAIC.exe --restore [dll]        restore unsafeAssem.dll from .orig
 *    PatchAIC.exe --help
 *
 *  Exit codes
 *  ----------
 *    0  patched OK  (or --restore succeeded)
 *    2  already patched, nothing to do
 *    3  target / pattern not found
 *    1  I/O or write failure
 *
 *  Build
 *  -----
 *    src\build.bat        -> ..\_out\PatchAIC.exe   (x64, static, KERNEL32 only)
 * ========================================================================= */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>
#include <stdarg.h>
#include <io.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define MAXFILE   (512u * 1024u * 1024u)
#define PATHCAP   (MAX_PATH * 2)

/* ======================================================================== */
/*  console / log                                                           */
/* ======================================================================== */

static FILE *g_log = NULL;

static void log_open(void)
{
    wchar_t self[PATHCAP], path[PATHCAP];
    size_t i;

    if (!GetModuleFileNameW(NULL, self, PATHCAP)) return;
    i = wcslen(self);
    while (i > 0 && self[i - 1] != L'\\' && self[i - 1] != L'/') i--;
    self[i] = 0;

    _snwprintf(path, PATHCAP - 1, L"%lsPatchAIC.log", self);
    path[PATHCAP - 1] = 0;
    g_log = _wfopen(path, L"w");
}

static void log_close(void)
{
    if (g_log) { fclose(g_log); g_log = NULL; }
}

static void out_a(const char *s)
{
    fputs(s, stdout);
    fflush(stdout);
    if (g_log) { fputs(s, g_log); fflush(g_log); }
}

static void out_w(const wchar_t *w)
{
    char buf[4096];
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, (int)sizeof(buf) - 1, NULL, NULL);
    if (n > 0) {
        buf[n - 1] = 0;
        fputs(buf, stdout);
        if (g_log) { fputs(buf, g_log); fflush(g_log); }
    }
    fflush(stdout);
}

static void out_af(const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsprintf_s(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out_a(buf);
}

static int pause_exit(int code)
{
    /* Only wait when a human can actually see the console.  Driven from
       patch.bat, a shortcut, or any redirected run, waiting here would just
       hang the caller. */
    if (_isatty(_fileno(stdin))) {
        out_a("\nPress Enter to exit...");
        (void)getchar();
    } else {
        out_a("\n");
    }
    log_close();
    return code;
}

/* ======================================================================== */
/*  small path helpers                                                      */
/* ======================================================================== */

static int path_exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int path_isdir(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void path_set(wchar_t *dst, size_t cap, const wchar_t *src)
{
    wcsncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
}

static void path_join(wchar_t *out, size_t cap, const wchar_t *a, const wchar_t *b)
{
    size_t la = wcslen(a);
    _snwprintf(out, cap - 1, L"%ls%ls%ls", a, (la && a[la - 1] != L'\\') ? L"\\" : L"", b);
    out[cap - 1] = 0;
}

static void path_dir(const wchar_t *path, wchar_t *out, size_t cap)
{
    size_t i = wcslen(path);
    while (i > 0 && path[i - 1] != L'\\' && path[i - 1] != L'/') i--;
    if (i >= cap) i = cap - 1;
    wmemcpy(out, path, i);
    out[i] = 0;
}

static void path_orig(wchar_t *out, size_t cap, const wchar_t *dll)
{
    _snwprintf(out, cap - 1, L"%ls.orig", dll);
    out[cap - 1] = 0;
}

/* ======================================================================== */
/*  file I/O                                                                */
/* ======================================================================== */

static u8 *file_load(const wchar_t *path, size_t *outSize)
{
    FILE *f = _wfopen(path, L"rb");
    u8 *buf;
    long sz;

    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    sz = ftell(f);
    if (sz <= 0 || (unsigned long)sz > MAXFILE) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

    buf = (u8 *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *outSize = (size_t)sz;
    return buf;
}

static int file_save(const wchar_t *path, const u8 *data, size_t n)
{
    FILE *f = _wfopen(path, L"wb");
    int ok = 0;
    if (!f) return 0;
    if (fwrite(data, 1, n, f) == n && fflush(f) == 0) ok = 1;
    fclose(f);
    return ok;
}

static int file_patch(const wchar_t *path, size_t off, const u8 *bytes, size_t n)
{
    FILE *f = _wfopen(path, L"r+b");
    int ok = 0;
    if (!f) return 0;
    if (fseek(f, (long)off, SEEK_SET) == 0 && fwrite(bytes, 1, n, f) == n && fflush(f) == 0)
        ok = 1;
    fclose(f);
    return ok;
}

/* ======================================================================== */
/*  PE parsing                                                              */
/* ======================================================================== */

typedef struct {
    const u8 *d;
    size_t    n;
    u32       numSec;
    u32       secOff;
    u32       cliRva;
    int       plus;
} PEFile;

static u16 rd16(const u8 *d, size_t n, size_t o)
{
    if (o + 2 > n) return 0;
    return (u16)(d[o] | ((u16)d[o + 1] << 8));
}

static u32 rd32(const u8 *d, size_t n, size_t o)
{
    if (o + 4 > n) return 0;
    return (u32)d[o] | ((u32)d[o + 1] << 8) | ((u32)d[o + 2] << 16) | ((u32)d[o + 3] << 24);
}

static long rva2off(const PEFile *f, u32 rva)
{
    u32 i;
    for (i = 0; i < f->numSec; i++) {
        u32 s     = f->secOff + i * 40;
        u32 vsize = rd32(f->d, f->n, s + 8);
        u32 va    = rd32(f->d, f->n, s + 12);
        u32 rsize = rd32(f->d, f->n, s + 16);
        u32 rptr  = rd32(f->d, f->n, s + 20);
        u32 span  = (vsize > rsize) ? vsize : rsize;
        if (rva >= va && rva < va + span && (rva - va) < rsize)
            return (long)(rptr + (rva - va));
    }
    return -1;
}

static int pe_open(PEFile *f, const u8 *data, size_t size)
{
    u32 lfanew, optSize, dirOff;
    u16 magic;

    memset(f, 0, sizeof(*f));
    f->d = data;
    f->n = size;

    if (size < 0x40) return 0;
    if (rd16(data, size, 0) != 0x5A4D) return 0;              /* MZ   */
    lfanew = rd32(data, size, 0x3C);
    if ((size_t)lfanew + 24 > size) return 0;
    if (rd32(data, size, lfanew) != 0x00004550) return 0;     /* PE\0\0 */

    f->numSec = rd16(data, size, lfanew + 6);
    optSize   = rd16(data, size, lfanew + 20);
    f->secOff = lfanew + 24 + optSize;
    if (f->numSec == 0 || f->numSec > 96) return 0;
    if ((size_t)f->secOff + (size_t)f->numSec * 40 > size) return 0;

    magic = rd16(data, size, lfanew + 24);
    if (magic == 0x20B)      { f->plus = 1; dirOff = lfanew + 24 + 112; }
    else if (magic == 0x10B) { f->plus = 0; dirOff = lfanew + 24 + 96;  }
    else return 0;

    f->cliRva = rd32(data, size, dirOff + 14 * 8);            /* COM descriptor */
    return f->cliRva != 0;
}

/* ======================================================================== */
/*  .NET metadata                                                           */
/* ======================================================================== */

typedef struct {
    const u8 *d;
    size_t    n;

    size_t strBase;      /* #Strings */
    size_t usBase;       /* #US      */
    size_t tblData;      /* first byte of table rows */

    u32 rows[64];
    u32 sStr, sGuid, sBlob;

    u32 tdOff, tdRow, tdMethOff;
    u32 mdOff, mdRow, mparamSz;
} Meta;

static const u32 TDR_TABLES[3] = { 0x02, 0x01, 0x1B };  /* TypeDef, TypeRef, TypeSpec   */
static const u32 RS_TABLES[4]  = { 0x00, 0x1A, 0x23, 0x01 }; /* Module, MethodSpec, ...  */

static u32 isz(u32 rows) { return (rows > 0xFFFFu) ? 4u : 2u; }

static u32 coded_size(const Meta *m, const u32 *tables, int ntables, int tagbits)
{
    u32 mx = 0, i;
    for (i = 0; i < (u32)ntables; i++)
        if (m->rows[tables[i]] > mx) mx = m->rows[tables[i]];
    return (mx < (1u << (16 - tagbits))) ? 2u : 4u;
}

static u32 read_idx(const Meta *m, size_t off, u32 size)
{
    return (size == 4) ? rd32(m->d, m->n, off) : (u32)rd16(m->d, m->n, off);
}

static int meta_open(Meta *m, const PEFile *f)
{
    long  cliOff;
    u32   metaRva, metaSize, metaOff;
    u32   verLen, p, streams, i;
    size_t strBase = 0, usBase = 0, tblBase = 0;
    u32   hs, validLo, validHi, t;

    memset(m, 0, sizeof(*m));
    m->d = f->d;
    m->n = f->n;

    cliOff = rva2off(f, f->cliRva);
    if (cliOff < 0 || (size_t)cliOff + 72 > f->n) return 0;

    metaRva  = rd32(f->d, f->n, (size_t)cliOff + 8);
    metaSize = rd32(f->d, f->n, (size_t)cliOff + 12);
    if (!metaRva || metaSize < 16) return 0;

    {
        long o = rva2off(f, metaRva);
        if (o < 0) return 0;
        metaOff = (u32)o;
    }
    if ((size_t)metaOff + metaSize > f->n) return 0;
    if (rd32(f->d, f->n, metaOff) != 0x424A5342) return 0;   /* BSJB */

    verLen = rd32(f->d, f->n, metaOff + 12);
    if (verLen == 0 || verLen > 256) return 0;
    p = metaOff + 16 + verLen;

    streams = rd16(f->d, f->n, p + 2);
    if (streams == 0 || streams > 32) return 0;
    p += 4;

    for (i = 0; i < streams; i++) {
        u32 sOff = rd32(f->d, f->n, p);
        u32 q = p + 8;
        char name[32];
        int k = 0;

        while (q < f->n && f->d[q] != 0 && k < 31) name[k++] = (char)f->d[q++];
        name[k] = 0;
        p = (q + 4) & ~3u;
        if (p > f->n) return 0;

        if (sOff && sOff < metaSize) {
            if (strcmp(name, "#Strings") == 0) strBase = (size_t)metaOff + sOff;
            else if (strcmp(name, "#US") == 0) usBase = (size_t)metaOff + sOff;
            else if (strcmp(name, "#~") == 0 || strcmp(name, "#-") == 0)
                tblBase = (size_t)metaOff + sOff;
        }
    }
    if (!strBase || !tblBase) return 0;

    /* table stream header */
    if (tblBase + 24 > f->n) return 0;
    hs = f->d[tblBase + 6];
    m->sStr  = (hs & 1) ? 4u : 2u;
    m->sGuid = (hs & 2) ? 4u : 2u;
    m->sBlob = (hs & 4) ? 4u : 2u;

    validLo = rd32(f->d, f->n, tblBase + 8);
    validHi = rd32(f->d, f->n, tblBase + 12);

    {
        u32 q = (u32)tblBase + 24;
        for (t = 0; t < 64; t++) {
            int set = (t < 32) ? (int)((validLo >> t) & 1) : (int)((validHi >> (t - 32)) & 1);
            if (set) { m->rows[t] = rd32(f->d, f->n, q); q += 4; }
        }
        m->tblData = q;
    }

    m->strBase = strBase;
    m->usBase  = usBase;

    {
        u32 rsSz  = coded_size(m, RS_TABLES, 4, 2);
        u32 tdrSz = coded_size(m, TDR_TABLES, 3, 2);
        u32 fi = isz(m->rows[0x04]);
        u32 mi = isz(m->rows[0x06]);
        u32 pi = isz(m->rows[0x08]);

        u32 szMod = 2 + m->sStr + m->sGuid * 3;
        u32 szRef = rsSz + m->sStr + m->sStr;
        u32 szTd  = 4 + m->sStr + m->sStr + tdrSz + fi + mi;
        u32 szFp  = fi;
        u32 szFl  = 2 + m->sStr + m->sBlob;
        u32 szMp  = mi;
        u32 szMd  = 4 + 2 + 2 + m->sStr + m->sBlob + pi;

        u32 off = (u32)m->tblData;
        off += m->rows[0x00] * szMod;
        off += m->rows[0x01] * szRef;

        m->tdOff    = off;
        m->tdRow    = szTd;
        m->tdMethOff = 4 + m->sStr + m->sStr + tdrSz + fi;

        off += m->rows[0x02] * szTd;
        off += m->rows[0x03] * szFp;
        off += m->rows[0x04] * szFl;
        off += m->rows[0x05] * szMp;

        m->mdOff     = off;
        m->mdRow     = szMd;
        m->mparamSz  = pi;

        off += m->rows[0x06] * szMd;
        if ((size_t)off > f->n) return 0;
    }
    return 1;
}

static const char *meta_str(const Meta *m, u32 idx)
{
    size_t o = m->strBase + idx;
    if (o >= m->n) return "";
    if (!memchr(m->d + o, 0, m->n - o)) return "";
    return (const char *)(m->d + o);
}

static int find_type(const Meta *m, const char *ns, const char *name, u32 *outRow)
{
    u32 i;
    for (i = 0; i < m->rows[0x02]; i++) {
        size_t r = (size_t)m->tdOff + (size_t)i * m->tdRow;
        u32 nameIdx = read_idx(m, r + 4, m->sStr);
        u32 nsIdx   = read_idx(m, r + 4 + m->sStr, m->sStr);
        if (_stricmp(meta_str(m, nameIdx), name) == 0 &&
            _stricmp(meta_str(m, nsIdx), ns) == 0) {
            *outRow = i;
            return 1;
        }
    }
    return 0;
}

static u32 typedef_methlist(const Meta *m, u32 typeRow)
{
    return read_idx(m, (size_t)m->tdOff + (size_t)typeRow * m->tdRow + m->tdMethOff, isz(m->rows[0x06]));
}

static int find_method(const Meta *m, u32 typeRow, const char *name, u32 *outRva)
{
    u32 total = m->rows[0x06];
    u32 start, end, i;

    if (typeRow >= m->rows[0x02]) return 0;
    start = typedef_methlist(m, typeRow);
    end   = (typeRow + 1 < m->rows[0x02]) ? typedef_methlist(m, typeRow + 1) : total + 1;
    if (end > total + 1) end = total + 1;

    for (i = start; i < end; i++) {
        u32 row = i - 1;                        /* method lists are 1-based */
        size_t r;
        u32 nameIdx;
        if (row >= total) break;
        r = (size_t)m->mdOff + (size_t)row * m->mdRow;
        nameIdx = read_idx(m, r + 8, m->sStr);
        if (_stricmp(meta_str(m, nameIdx), name) == 0) {
            *outRva = rd32(m->d, m->n, r);
            return 1;
        }
    }
    return 0;
}

/* user-string lookup: token 0x70xxxxxx -> UTF-16LE bytes in the #US heap */
static int us_equals(const Meta *m, u32 token, const char *ascii)
{
    size_t o, len, k, alen = strlen(ascii);
    u32 b0;

    if (!m->usBase) return 0;
    if ((token & 0xFF000000u) != 0x70000000u) return 0;

    o = m->usBase + (token & 0x00FFFFFFu);
    if (o + 1 >= m->n) return 0;

    b0 = m->d[o];
    if ((b0 & 0x80) == 0) {                     /* 1-byte compressed length */
        len = b0;
        o += 1;
    } else if ((b0 & 0xC0) == 0x80) {           /* 2-byte */
        if (o + 1 >= m->n) return 0;
        len = ((size_t)(b0 & 0x3F) << 8) | m->d[o + 1];
        o += 2;
    } else {                                    /* 4-byte */
        if (o + 3 >= m->n) return 0;
        len = ((size_t)(b0 & 0x1F) << 24) | ((size_t)m->d[o + 1] << 16)
            | ((size_t)m->d[o + 2] << 8) | m->d[o + 3];
        o += 4;
    }
    if (len < 2 || len > 0x10000) return 0;
    len -= 1;                                   /* drop the trailing flag byte */
    if (len != alen * 2) return 0;
    if (o + len > m->n) return 0;

    for (k = 0; k < alen; k++)
        if (m->d[o + k * 2] != (u8)ascii[k] || m->d[o + k * 2 + 1] != 0) return 0;
    return 1;
}

/* ======================================================================== */
/*  IL body                                                                 */
/* ======================================================================== */

typedef struct {
    long off;        /* file offset of the first IL byte */
    u32  size;       /* IL byte count                    */
} Body;

static int method_body(const PEFile *f, u32 rva, Body *out)
{
    long o = rva2off(f, rva);
    u8   first;

    if (o < 0 || (size_t)o + 1 > f->n) return 0;
    first = f->d[o];

    if ((first & 3) == 2) {                     /* tiny header, 1 byte */
        out->size = (u32)(first >> 2);
        out->off  = o + 1;
    } else if ((first & 3) == 3) {              /* fat header */
        u16 flags = rd16(f->d, f->n, (size_t)o);
        u32 hdr   = (u32)(flags >> 12) * 4u;
        if (hdr < 12) hdr = 12;
        out->size = rd32(f->d, f->n, (size_t)o + 4);
        out->off  = o + (long)hdr;
    } else {
        return 0;
    }
    if (out->size == 0 || out->size > 0x100000) return 0;
    if ((size_t)out->off + out->size > f->n) return 0;
    return 1;
}

/* ======================================================================== */
/*  patch detection                                                         */
/* ======================================================================== */

/*  unpatched:  brfalse.s +2 ; ldc.i4.0 ; stloc.0 ; ldloc.0 ; brtrue.s ...
    0x2C 02 | 0x16 0x0A | 0x06 0x2D 0x16

    patched  :  brfalse.s +2 ; nop      ; nop     ; ldloc.0 ; brtrue.s ...
    0x2C 02 | 0x00 0x00 | 0x06 0x2D 0x16                          */
static const u8 ANCHOR_HOT[7]  = { 0x2C, 0x02, 0x16, 0x0A, 0x06, 0x2D, 0x16 };
static const u8 ANCHOR_DONE[7] = { 0x2C, 0x02, 0x00, 0x00, 0x06, 0x2D, 0x16 };

enum { R_OK = 0, R_ALREADY = 2, R_NOTFOUND = 3 };

typedef struct {
    int    status;      /* R_OK / R_ALREADY / R_NOTFOUND */
    size_t fileOff;     /* absolute offset of ldc.i4.0 (or the first nop) */
    u32    ilOffset;    /* IL offset inside the method body */
    int    sawVirgl;
} Result;

static int analyze(const u8 *d, size_t n, Result *res)
{
    PEFile f;
    Meta   m;
    u32    typeRow = 0, methRva = 0;
    Body   body;
    u32    i;
    long   hot = -1, done = -1;
    int    hotCount = 0, doneCount = 0;

    memset(res, 0, sizeof(*res));
    res->status = R_NOTFOUND;

    if (!pe_open(&f, d, n)) {
        out_a("[!] not a PE file\n");
        return R_NOTFOUND;
    }
    if (!meta_open(&m, &f)) {
        if (!f.cliRva)
            out_a("[!] this is not a .NET assembly (no CLR header)\n");
        else
            out_a("[!] cannot parse .NET metadata\n");
        return R_NOTFOUND;
    }
    if (!find_type(&m, "XX", "IN", &typeRow)) {
        out_a("[!] type XX.IN not found\n");
        return R_NOTFOUND;
    }
    if (!find_method(&m, typeRow, "Awake", &methRva)) {
        out_a("[!] method XX.IN::Awake not found\n");
        return R_NOTFOUND;
    }
    if (!method_body(&f, methRva, &body)) {
        out_a("[!] cannot read the IL body of XX.IN::Awake\n");
        return R_NOTFOUND;
    }

    /* the method must actually reference the "virgl" detection literal */
    for (i = 0; i + 5 <= body.size; i++) {
        size_t o = (size_t)body.off + i;
        if (d[o] == 0x72) {                     /* ldstr <token> */
            u32 tok = rd32(d, n, o + 1);
            if (us_equals(&m, tok, "virgl")) { res->sawVirgl = 1; break; }
        }
    }

    /* locate the pattern, but only inside this method's body */
    for (i = 0; i + 7 <= body.size; i++) {
        const u8 *c = d + (size_t)body.off + i;
        size_t    o = (size_t)body.off + i;
        if (memcmp(c, ANCHOR_HOT, 7) == 0)  { hotCount++;  if (hot  < 0) hot  = (long)o; }
        if (memcmp(c, ANCHOR_DONE, 7) == 0) { doneCount++; if (done < 0) done = (long)o; }
    }

    if (!res->sawVirgl)
        out_a("[!] warning: XX.IN::Awake does not reference the 'virgl' marker\n");

    if (hotCount == 1) {
        res->fileOff  = (size_t)hot + 2;
        res->ilOffset = (u32)((size_t)hot + 2 - (size_t)body.off);
        res->status   = R_OK;
        return R_OK;
    }
    if (hotCount > 1) {
        out_a("[!] the pattern is ambiguous inside XX.IN::Awake (multiple matches)\n");
        return R_NOTFOUND;
    }
    if (doneCount == 1) {
        res->fileOff  = (size_t)done + 2;
        res->ilOffset = (u32)((size_t)done + 2 - (size_t)body.off);
        res->status   = R_ALREADY;
        return R_ALREADY;
    }
    if (doneCount > 1) {
        out_a("[!] multiple patched markers inside XX.IN::Awake\n");
        return R_NOTFOUND;
    }

    out_a("[!] env-check pattern (ldc.i4.0 ; stloc.0 ; ldloc.0) not found\n");
    return R_NOTFOUND;
}

/* ======================================================================== */
/*  target discovery                                                        */
/* ======================================================================== */

static int find_deep(const wchar_t *dir, wchar_t *out, size_t cap, int depth)
{
    WIN32_FIND_DATAW fd;
    HANDLE h;
    wchar_t pat[PATHCAP];

    if (depth > 5) return 0;

    path_join(pat, PATHCAP, dir, L"*");
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    /* first pass: a direct child named unsafeAssem.dll */
    do {
        if (fd.cFileName[0] == L'.') continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (_wcsicmp(fd.cFileName, L"unsafeAssem.dll") == 0) {
            path_join(out, cap, dir, fd.cFileName);
            FindClose(h);
            return 1;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    /* second pass: recurse into subdirectories */
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.cFileName[0] == L'.') continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        {
            wchar_t sub[PATHCAP];
            path_join(sub, PATHCAP, dir, fd.cFileName);
            if (find_deep(sub, out, cap, depth + 1)) { FindClose(h); return 1; }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}

/* accept a DLL, or a folder (game root / Data / Managed) */
static int resolve_arg(const wchar_t *arg, wchar_t *out, size_t cap)
{
    static const wchar_t *SUBS[] = {
        L"AliceInCradle_Data\\Managed\\unsafeAssem.dll",
        L"Data\\Managed\\unsafeAssem.dll",
        L"Managed\\unsafeAssem.dll",
        L"unsafeAssem.dll"
    };
    size_t i;

    if (!arg || !arg[0]) return 0;
    if (path_exists(arg)) { path_set(out, cap, arg); return 1; }
    if (!path_isdir(arg))  return 0;

    for (i = 0; i < 4; i++) {
        wchar_t t[PATHCAP];
        path_join(t, PATHCAP, arg, SUBS[i]);
        if (path_exists(t)) { path_set(out, cap, t); return 1; }
    }
    return find_deep(arg, out, cap, 0);
}

/* look around the exe itself */
static int resolve_auto(wchar_t *out, size_t cap)
{
    static const wchar_t *SUBS[] = {
        L"unsafeAssem.dll",
        L"AliceInCradle_Data\\Managed\\unsafeAssem.dll",
        L"..\\AliceInCradle_Data\\Managed\\unsafeAssem.dll",
        L"..\\..\\AliceInCradle_Data\\Managed\\unsafeAssem.dll",
        L"Data\\Managed\\unsafeAssem.dll",
        L"Managed\\unsafeAssem.dll"
    };
    wchar_t self[PATHCAP], dir[PATHCAP];
    size_t i;

    if (!GetModuleFileNameW(NULL, self, PATHCAP)) return 0;
    path_dir(self, dir, PATHCAP);

    for (i = 0; i < 6; i++) {
        wchar_t t[PATHCAP];
        path_join(t, PATHCAP, dir, SUBS[i]);
        if (path_exists(t)) { path_set(out, cap, t); return 1; }
    }
    return find_deep(dir, out, cap, 0);
}

/* ======================================================================== */
/*  actions                                                                 */
/* ======================================================================== */

static int do_check(const wchar_t *dll)
{
    u8 *d;
    size_t n = 0;
    Result r;

    d = file_load(dll, &n);
    if (!d) { out_a("[!] cannot read the file\n"); return R_NOTFOUND; }

    analyze(d, n, &r);
    free(d);

    if (r.status == R_OK) {
        out_af("[=] NOT patched  - env-check found at IL_%04X / IL_%04X\n",
               r.ilOffset, r.ilOffset + 1);
        return R_OK;
    }
    if (r.status == R_ALREADY) {
        out_af("[=] ALREADY patched - nop markers at IL_%04X / IL_%04X\n",
               r.ilOffset, r.ilOffset + 1);
        return R_ALREADY;
    }
    return R_NOTFOUND;
}

static int do_restore(const wchar_t *dll)
{
    wchar_t orig[PATHCAP];

    path_orig(orig, PATHCAP, dll);
    if (!path_exists(orig)) {
        out_a("[!] no backup found (unsafeAssem.dll.orig).\n");
        return R_NOTFOUND;
    }
    if (!CopyFileW(orig, dll, FALSE)) {
        out_af("[!] restore failed (error %lu)\n", (unsigned long)GetLastError());
        return 1;
    }
    out_a("[+] restored unsafeAssem.dll from unsafeAssem.dll.orig\n");
    return R_OK;
}

static int do_patch(const wchar_t *dll, const wchar_t *outPath)
{
    u8    *d;
    size_t n = 0;
    Result r;
    wchar_t backup[PATHCAP];

    d = file_load(dll, &n);
    if (!d) { out_a("[!] cannot read the file\n"); return R_NOTFOUND; }

    analyze(d, n, &r);

    if (r.status == R_ALREADY) {
        out_a("[=] this DLL is ALREADY patched - nothing to do.\n");
        free(d);
        return R_ALREADY;
    }
    if (r.status != R_OK) {
        out_a("[!] PATCH FAILED - pattern not located.\n");
        out_a("    The game version probably changed its detection logic.\n");
        free(d);
        return R_NOTFOUND;
    }

    out_af("[+] found env-check flag=false at IL_%04X / IL_%04X\n",
           r.ilOffset, r.ilOffset + 1);

    if (outPath && outPath[0]) {
        /* out-of-place: write a patched copy, leave the original untouched */
        d[r.fileOff]     = 0x00;
        d[r.fileOff + 1] = 0x00;
        if (!file_save(outPath, d, n)) {
            out_a("[!] cannot write the output file\n");
            free(d);
            return 1;
        }
        free(d);
        out_a("[+] PATCHED OK -> ");
        out_w(outPath);
        out_a("\n");
        return R_OK;
    }

    /* in place: backup once, then flip the two bytes */
    path_orig(backup, PATHCAP, dll);
    if (!path_exists(backup)) {
        if (CopyFileW(dll, backup, TRUE))
            out_a("[+] backup created: unsafeAssem.dll.orig\n");
        else
            out_af("[!] warning: backup failed (error %lu), continuing\n",
                   (unsigned long)GetLastError());
    } else {
        out_a("[=] backup already exists, kept as is\n");
    }

    {
        const u8 nop2[2] = { 0x00, 0x00 };
        if (!file_patch(dll, r.fileOff, nop2, 2)) {
            out_a("[!] write failed\n");
            free(d);
            return 1;
        }
    }
    free(d);

    out_a("[+] PATCHED OK\n");
    return R_OK;
}

/* ======================================================================== */
/*  entry                                                                   */
/* ======================================================================== */

static void banner(void)
{
    out_a("=========================================\n");
    out_a(" Alice in Cradle - VM/env-check patcher\n");
    out_a(" native build, no .NET required\n");
    out_a("=========================================\n\n");
}

static void usage(void)
{
    out_a("usage:\n");
    out_a("  PatchAIC.exe [dll-or-folder]      patch in place\n");
    out_a("  PatchAIC.exe <in.dll> <out.dll>   patch into a new file\n");
    out_a("  PatchAIC.exe --check  [dll]       report status only\n");
    out_a("  PatchAIC.exe --restore [dll]      restore from unsafeAssem.dll.orig\n");
    out_a("  PatchAIC.exe --help\n");
    out_a("\n");
    out_a("If no path is given the tool looks for unsafeAssem.dll next to\n");
    out_a("itself, in ..\\AliceInCradle_Data\\Managed\\, and recursively below\n");
    out_a("its own folder. You can also drop a DLL or a game folder on it.\n");
}

static int is_switch(const wchar_t *s, const wchar_t *name)
{
    return _wcsicmp(s, name) == 0;
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t target[PATHCAP] = { 0 };
    wchar_t outPath[PATHCAP] = { 0 };
    wchar_t input[PATHCAP];
    wchar_t self[PATHCAP], dir[PATHCAP];
    int     mode = 0;               /* 0 patch, 1 check, 2 restore */
    int     rc;
    int     haveArg = 0;

    log_open();
    banner();

    /* ---- switches ---- */
    if (argc >= 2) {
        const wchar_t *a = argv[1];
        if (is_switch(a, L"-h") || is_switch(a, L"--help") || is_switch(a, L"/?")) {
            usage();
            return pause_exit(0);
        }
        if (is_switch(a, L"--check") || is_switch(a, L"-c"))   { mode = 1; a = (argc >= 3) ? argv[2] : NULL; }
        else if (is_switch(a, L"--restore") || is_switch(a, L"-r")) { mode = 2; a = (argc >= 3) ? argv[2] : NULL; }

        if (a && a[0]) {
            if (!resolve_arg(a, target, PATHCAP)) {
                out_a("[!] file not found: ");
                out_w(a);
                out_a("\n");
                return pause_exit(R_NOTFOUND);
            }
            haveArg = 1;
        }
    }
    if (argc >= 3 && argv[2][0] && mode == 0)
        path_set(outPath, PATHCAP, argv[2]);

    /* ---- discovery / interaction ---- */
    if (!haveArg) {
        if (!resolve_auto(target, PATHCAP)) {
            GetModuleFileNameW(NULL, self, PATHCAP);
            path_dir(self, dir, PATHCAP);
            out_a("[*] exe folder: ");
            out_w(dir);
            out_a("\n");
            out_a("[*] unsafeAssem.dll was not found automatically.\n");
            out_a("[*] Type the full path of unsafeAssem.dll\n");
            out_a("    (or drop the DLL / game folder onto this exe), then press Enter.\n");
            out_a("    Path: ");

            if (!fgetws(input, PATHCAP, stdin)) return pause_exit(R_NOTFOUND);
            {
                size_t L = wcslen(input);
                while (L && (input[L - 1] == L'\n' || input[L - 1] == L'\r' ||
                             input[L - 1] == L'"'  || input[L - 1] == L' ')) input[--L] = 0;
                if (input[0] == L'"') wmemmove(input, input + 1, wcslen(input) + 1);
            }
            if (!resolve_arg(input, target, PATHCAP)) {
                out_a("[!] file not found.\n");
                return pause_exit(R_NOTFOUND);
            }
        }
    }

    out_a("[*] target: ");
    out_w(target);
    out_a("\n");

    switch (mode) {
    case 1:
        rc = do_check(target);
        break;
    case 2:
        rc = do_restore(target);
        break;
    default:
        rc = do_patch(target, outPath);
        break;
    }

    if (mode == 0 && rc == R_OK && !outPath[0]) {
        out_a("\nDone. Start the game now.\n");
        out_a("To restore: copy unsafeAssem.dll.orig back over unsafeAssem.dll\n");
        out_a("        or: PatchAIC.exe --restore\n");
    }
    return pause_exit(rc);
}
