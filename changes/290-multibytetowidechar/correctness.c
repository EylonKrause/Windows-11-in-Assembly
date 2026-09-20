/* changes/290-multibytetowidechar/correctness.c
 *
 * THREE-WAY: our assembly, a scalar reference, and the LIVE kernel32!MultiByteToWideChar.
 * A single mismatch fails.  Every comparison checks FOUR things, because this function has four
 * observable outputs and three of them are not the return value:
 *
 *      the return value,  GetLastError(),  every byte of the destination up to the capacity,
 *      and that nothing was written at or past the capacity.
 *
 * Why the corpus looks like this.
 *
 * 1.  a random byte fuzz is not enough, and change 034 wrote down why: it draws each byte's class
 *     independently, so sixteen bytes that happen to be eight clean two-byte sequences has a
 *     probability of about 1e-11 per position.  Every vector block would be untested.  So the
 *     corpus is explicit about the classes: pure ASCII, pure two-, three- and four-byte runs, and
 *, new here, and the whole reason this change does not reuse change 034's decoder --
 *     MIXED-WIDTH classes: ASCII+2, ASCII+3, ASCII+4, 2+3, 3+4, and all four widths rotating.
 *     discovery/utf8_width_mixtures.c is the file that proved a run-only decoder collapses on them.
 *
 * 2.  Every length from 0 to 200 and every capacity from 0 to 2x the length, so that a block
 *     boundary falls inside every class at some length and the room guard of every block is the
 *     one that decides where the output stops.
 *
 * 3.  a malformed byte planted at every position of every class, eight of them, one per way a
 *     sequence can be wrong, because the maximal-subpart rule is the part that is not guessable
 *     and the part every vector block must decline.
 *
 * 4.  The source ends at a page boundary with the next page PAGE_NOACCESS.  gen16 reads eighteen
 *     bytes to consume at most sixteen and the three-byte block reads twenty-eight to consume
 *     twenty-four; their guards are the only thing keeping those loads inside the caller's buffer,
 *     and with the source in a static array an over-read is invisible.
 *
 * 5.  The destination ends at a page boundary too, with its capacity as the last thing before an
 *     unmapped page.  A block that stores sixteen bytes and advances by fewer would otherwise
 *     write into slack no assertion looks at.
 *
 * 6.  The parameter and flag matrix, which is where this function hides most of its contract:
 *     the alias rule, the 0x0F flag mask, the precedence of ERROR_INVALID_PARAMETER over
 *     ERROR_INVALID_FLAGS, and of ERROR_INSUFFICIENT_BUFFER over ERROR_NO_UNICODE_TRANSLATION.
 *
 * 7.  The dispatch boundary: for every code page that is not 65001, ours must be
 *     INDISTINGUISHABLE from the shipped export, because it tail-calls it.
 *
 * 8.  The two assembler-generated tables, checked against the same rules written in C.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

extern int  wia_mbtwc(UINT, DWORD, const char*, int, wchar_t*, int);
extern void wia_mbtwc_set_fallback(void*);
extern const unsigned char* wia_mbtwc_mix_table(void);
extern const unsigned int*  wia_mbtwc_lead_table(void);
extern const unsigned short* wia_mbtwc_store_masks(void);
int ref_mbtwc(UINT, DWORD, const char*, int, wchar_t*, int);

typedef int (WINAPI *FN)(UINT, DWORD, LPCCH, int, LPWSTR, int);
static FN sys;

static long long cases = 0;
static long long failures = 0;

#define DW 1024
#define FILLW 0x2323

static int slack(const char* who, const wchar_t* d, int cch, int n, int cch_reported)
{
    int i;
    for (i = cch; i < DW; ++i)
        if (d[i] != (wchar_t)FILLW) {
            printf("FAIL n=%d cch=%d: %s wrote %04X at unit %d, PAST the capacity\n",
                   n, cch, who, d[i], i);
            (void)cch_reported;
            return 1;
        }
    return 0;
}

static void one(const unsigned char* s, int n, int cb, int cch, DWORD fl)
{
    static wchar_t da[DW], db[DW], dc[DW];
    int ra, rb, rc, i, bad = 0;
    DWORD ea, eb, ec;
    ++cases;
    for (i = 0; i < DW; ++i) da[i] = db[i] = dc[i] = (wchar_t)FILLW;

    SetLastError(0xD1CE); ra = sys(CP_UTF8, fl, (const char*)s, cb, cch ? da : NULL, cch); ea = GetLastError();
    SetLastError(0xD1CE); rb = wia_mbtwc(CP_UTF8, fl, (const char*)s, cb, cch ? db : NULL, cch); eb = GetLastError();
    SetLastError(0xD1CE); rc = ref_mbtwc(CP_UTF8, fl, (const char*)s, cb, cch ? dc : NULL, cch); ec = GetLastError();

    if (ra != rb || rb != rc || ea != eb || eb != ec) bad = 1;
    if (!bad && cch)
        for (i = 0; i < cch && i < DW; ++i)
            if (da[i] != db[i] || db[i] != dc[i]) { bad = 1; break; }
    if (bad) {
        if (failures < 25) {
            printf("FAIL n=%d cb=%d cch=%d fl=%lu : live %d/%lu  ours %d/%lu  ref %d/%lu\n",
                   n, cb, cch, (unsigned long)fl, ra, (unsigned long)ea, rb, (unsigned long)eb,
                   rc, (unsigned long)ec);
            printf("     src :"); for (i = 0; i < n && i < 16; ++i) printf(" %02X", s[i]); printf("\n");
            if (cch) {
                printf("     live:"); for (i = 0; i < 10 && i < cch; ++i) printf(" %04X", da[i]); printf("\n");
                printf("     ours:"); for (i = 0; i < 10 && i < cch; ++i) printf(" %04X", db[i]); printf("\n");
                printf("     ref :"); for (i = 0; i < 10 && i < cch; ++i) printf(" %04X", dc[i]); printf("\n");
            }
        }
        ++failures;
    }
    if (cch) {
        if (slack("ours", db, cch, n, rb)) ++failures;
        if (slack("the reference", dc, cch, n, rc)) ++failures;
        if (slack("the live export", da, cch, n, ra)) ++failures;
    }
}

/* ---------------------------------------------------------------------------------------------
 * the input classes.  Five are runs (the shape change 034's blocks were written for) and six
 * are MIXTURES, which is the shape real text has and the shape a run-only decoder collapses on.
 * ------------------------------------------------------------------------------------------- */
enum { K_ASCII, K_TWO, K_THREE, K_FOUR, K_FFFD, K_A2, K_A3, K_A4, K_23, K_34, K_ALL, K_CONT, NCLASS };
static const char* KNAME[NCLASS] = {
    "ASCII", "2-byte", "3-byte", "4-byte", "U+FFFD", "ASCII+2", "ASCII+3", "ASCII+4",
    "2+3", "3+4", "1+2+3+4", "continuations"
};

static void put2(unsigned char* s, int* pi, int n, int seed)
{ if (*pi + 1 < n) { s[(*pi)++] = (unsigned char)(0xC2 + (seed % 0x1E)); s[(*pi)++] = (unsigned char)(0x80 + (seed % 0x40)); } else s[(*pi)++] = 'z'; }
static void put3(unsigned char* s, int* pi, int n, int seed)
{ if (*pi + 2 < n) { s[(*pi)++] = (unsigned char)(0xE1 + (seed % 0x0C)); s[(*pi)++] = (unsigned char)(0x80 + (seed % 0x40)); s[(*pi)++] = (unsigned char)(0x80 + ((seed >> 2) % 0x40)); } else s[(*pi)++] = 'z'; }
static void put4(unsigned char* s, int* pi, int n, int seed)
{
    int lead = seed % 4;                      /* F0..F3: a run built only from F0 leaves the
                                                 lead's three payload bits at zero, so the shift
                                                 that places them would be unobservable */
    if (*pi + 3 < n) {
        s[(*pi)++] = (unsigned char)(0xF0 + lead);
        s[(*pi)++] = (unsigned char)((lead ? 0x80 : 0x90) + (seed % 0x30));
        s[(*pi)++] = (unsigned char)(0x80 + (seed % 0x40));
        s[(*pi)++] = (unsigned char)(0x80 + ((seed >> 3) % 0x40));
    } else s[(*pi)++] = 'z';
}

static void build(int kind, unsigned char* s, int n)
{
    int i = 0, t = 0;
    while (i < n) {
        switch (kind) {
        case K_ASCII: s[i] = (unsigned char)('a' + (i % 26)); ++i; break;
        case K_TWO:   put2(s, &i, n, t); break;
        case K_THREE: put3(s, &i, n, t); break;
        case K_FOUR:  put4(s, &i, n, t); break;
        case K_FFFD:  if (i + 2 < n) { s[i++] = 0xEF; s[i++] = 0xBF; s[i++] = 0xBD; } else s[i++] = 'z'; break;
        case K_A2:    if ((t & 1) == 0) s[i++] = (unsigned char)('a' + (i % 26)); else put2(s, &i, n, t); break;
        case K_A3:    if ((t & 1) == 0) s[i++] = (unsigned char)('a' + (i % 26)); else put3(s, &i, n, t); break;
        case K_A4:    if ((t & 1) == 0) s[i++] = (unsigned char)('a' + (i % 26)); else put4(s, &i, n, t); break;
        case K_23:    if ((t & 1) == 0) put2(s, &i, n, t); else put3(s, &i, n, t); break;
        case K_34:    if ((t & 1) == 0) put3(s, &i, n, t); else put4(s, &i, n, t); break;
        case K_ALL:   switch (t & 3) {
                      case 0: s[i++] = (unsigned char)('a' + (i % 26)); break;
                      case 1: put2(s, &i, n, t); break;
                      case 2: put3(s, &i, n, t); break;
                      default: put4(s, &i, n, t); break; } break;
        default:      s[i] = (unsigned char)(0x80 + (i & 0x3F)); ++i; break;
        }
        ++t;
    }
}

/* --------------------------------------------------------------------------------------------- */
static void table_checks(void)
{
    const unsigned char* T = wia_mbtwc_mix_table();
    const unsigned int*  U = wia_mbtwc_lead_table();
    int idx, c, bad = 0;
    for (idx = 0; idx < 256; ++idx) {
        unsigned char want[16]; int o = 0, j, k;
        for (j = 0; j < 8; ++j) if ((idx >> j) & 1) { want[o++] = (unsigned char)(2*j); want[o++] = (unsigned char)(2*j+1); }
        for (k = o; k < 16; ++k) want[k] = 0x80;
        for (k = 0; k < 16; ++k) if (T[idx*16 + k] != want[k]) {
            printf("FAIL compaction table entry %d byte %d: %02X want %02X\n", idx, k, T[idx*16+k], want[k]);
            bad = 1;
        }
    }
    for (c = 0; c < 256; ++c) {
        unsigned len, lo, hi, val, want;
        if (c < 0xC2 || c > 0xF4) want = 0;
        else {
            len = (c < 0xE0) ? 2u : (c < 0xF0) ? 3u : 4u;
            lo = 0x80; hi = 0xBF;
            if (c == 0xE0) lo = 0xA0; else if (c == 0xED) hi = 0x9F;
            else if (c == 0xF0) lo = 0x90; else if (c == 0xF4) hi = 0x8F;
            val = (len == 2) ? (unsigned)(c & 0x1F) : (len == 3) ? (unsigned)(c & 0x0F) : (unsigned)(c & 7);
            want = len | ((lo - 0x80u) << 8) | ((hi - lo) << 16) | (val << 24);
        }
        if (U[c] != want) {
            printf("FAIL lead table entry %02X: %08X want %08X\n", c, U[c], want);
            bad = 1;
        }
    }
    if (bad) ++failures;
    printf("  tables: %s\n", bad ? "MISMATCH" : "ok (256 compaction entries, 256 lead entries)");
}

/* --------------------------------------------------------------------------------------------- */
static void source_page_guard(void)
{
    SYSTEM_INFO si;
    SIZE_T pg;
    char *base, *p1;
    static wchar_t d[DW];
    int n, k, faults = 0, wrong = 0;
    unsigned st = 7;
    GetSystemInfo(&si);
    pg = si.dwPageSize;
    base = (char*)VirtualAlloc(NULL, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
    p1 = (char*)VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE);

    for (k = 0; k < NCLASS; ++k) {
        for (n = 1; n <= 96; ++n) {
            unsigned char* s = (unsigned char*)(p1 + pg - n);
            int ra, rb, cch;
            build(k, s, n);
            for (cch = 0; cch <= 2; ++cch) {
                ++cases;
                ra = sys(CP_UTF8, 0, (const char*)s, n, cch ? d : NULL, cch ? DW : 0);
                __try {
                    rb = wia_mbtwc(CP_UTF8, 0, (const char*)s, n, cch ? d : NULL, cch ? DW : 0);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    printf("FAIL over-read: class %s n=%d faulted\n", KNAME[k], n);
                    ++faults; ++failures; rb = -1;
                }
                if (rb != ra) { ++wrong; ++failures;
                    if (wrong < 5) printf("FAIL page-guarded class %s n=%d: ours %d live %d\n", KNAME[k], n, rb, ra); }
            }
        }
    }
    /* random bytes, which is what actually walks the malformed paths off the end of a page */
    for (n = 1; n <= 96; ++n) {
        int t;
        for (t = 0; t < 300; ++t) {
            unsigned char* s = (unsigned char*)(p1 + pg - n);
            int i, ra, rb;
            for (i = 0; i < n; ++i) { st = st * 1103515245u + 12345u; s[i] = (unsigned char)(st >> 17); }
            ++cases;
            ra = sys(CP_UTF8, 0, (const char*)s, n, d, DW);
            __try { rb = wia_mbtwc(CP_UTF8, 0, (const char*)s, n, d, DW); }
            __except (EXCEPTION_EXECUTE_HANDLER) { printf("FAIL over-read: random n=%d\n", n); ++faults; ++failures; rb = -1; }
            if (rb != ra) { ++wrong; ++failures; }
        }
    }
    printf("  source page guard: %d faults, %d wrong counts\n", faults, wrong);
}

/* the destination's capacity is the last thing before an unmapped page */
static void dest_page_guard(void)
{
    SYSTEM_INFO si;
    SIZE_T pg;
    char *base, *p1;
    static unsigned char s[512];
    static wchar_t rd[DW];
    int n, k, cch, faults = 0, wrong = 0;
    GetSystemInfo(&si);
    pg = si.dwPageSize;
    base = (char*)VirtualAlloc(NULL, pg * 2, MEM_RESERVE, PAGE_NOACCESS);
    p1 = (char*)VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE);

    for (k = 0; k < NCLASS; ++k) {
        for (n = 1; n <= 80; ++n) {
            build(k, s, n);
            for (cch = 0; cch <= n + 2; ++cch) {
                wchar_t* d = (wchar_t*)(p1 + pg - (SIZE_T)cch * 2);
                int ra, rb, i; DWORD ea, eb;
                ++cases;
                for (i = 0; i < DW; ++i) rd[i] = (wchar_t)FILLW;
                for (i = 0; i < cch; ++i) d[i] = (wchar_t)FILLW;
                SetLastError(0xD1CE); ra = sys(CP_UTF8, 0, (const char*)s, n, cch ? rd : NULL, cch); ea = GetLastError();
                SetLastError(0xD1CE);
                __try { rb = wia_mbtwc(CP_UTF8, 0, (const char*)s, n, cch ? d : NULL, cch); }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    printf("FAIL over-write: class %s n=%d cch=%d faulted\n", KNAME[k], n, cch);
                    ++faults; ++failures; rb = -1;
                }
                eb = GetLastError();
                if (rb != ra || eb != ea) { ++wrong; ++failures;
                    if (wrong < 5) printf("FAIL tight dest class %s n=%d cch=%d: ours %d/%lu live %d/%lu\n",
                                          KNAME[k], n, cch, rb, (unsigned long)eb, ra, (unsigned long)ea); }
                else for (i = 0; i < cch; ++i) if (d[i] != rd[i]) { ++wrong; ++failures; break; }
            }
        }
    }
    printf("  destination page guard: %d faults, %d wrong\n", faults, wrong);
}

/* --------------------------------------------------------------------------------------------- */
static void parameter_matrix(void)
{
    static wchar_t a[64], b[64], c[64];
    static const struct { const char* tag; DWORD fl; int srcnull; int cb; int dstnull; int cch; } P[] = {
        { "cb=0",              0, 0,  0, 0,  8 },
        { "cb=0 cch=0",        0, 0,  0, 1,  0 },
        { "cch<0",             0, 0,  3, 0, -1 },
        { "cch=INT_MIN",       0, 0,  3, 0, INT_MIN },
        { "src=NULL",          0, 1,  3, 0,  8 },
        { "src=NULL cb=-1",    0, 1, -1, 0,  8 },
        { "src=NULL cch=0",    0, 1,  3, 1,  0 },
        { "dst=NULL cch>0",    0, 0,  3, 1,  8 },
        { "dst=NULL cch=0",    0, 0,  3, 1,  0 },
        { "flags 0x01",     0x01, 0,  3, 0,  8 },
        { "flags 0x02",     0x02, 0,  3, 0,  8 },
        { "flags 0x04",     0x04, 0,  3, 0,  8 },
        { "flags 0x07",     0x07, 0,  3, 0,  8 },
        { "flags 0x08",     0x08, 0,  3, 0,  8 },
        { "flags 0x0F",     0x0F, 0,  3, 0,  8 },
        { "flags 0x10",     0x10, 0,  3, 0,  8 },
        { "flags 0x20",     0x20, 0,  3, 0,  8 },
        { "flags 0x80",     0x80, 0,  3, 0,  8 },
        { "flags 0x100",   0x100, 0,  3, 0,  8 },
        { "flags 0x8000",0x8000, 0,  3, 0,  8 },
        { "flags high", 0x80000000u, 0, 3, 0, 8 },
        { "flags+cb0",     0x100, 0,  0, 0,  8 },
        { "flags+cch<0",   0x100, 0,  3, 0, -1 },
        { "flags+srcnull", 0x100, 1,  3, 0,  8 },
        { "cb=-1",             0, 0, -1, 0,  8 },
        { "cb=-2",             0, 0, -2, 0,  8 },
        { "cb=INT_MIN",        0, 0, INT_MIN, 0, 8 },
        { "cb=4 with NUL",     0, 0,  4, 0,  8 },
    };
    int i;
    const char* src = "abc";
    for (i = 0; i < (int)(sizeof P / sizeof P[0]); ++i) {
        int ra, rb, rc, j, bad = 0; DWORD ea, eb, ec;
        ++cases;
        for (j = 0; j < 64; ++j) a[j] = b[j] = c[j] = (wchar_t)FILLW;
        SetLastError(0xD1CE); ra = sys(CP_UTF8, P[i].fl, P[i].srcnull ? NULL : src, P[i].cb, P[i].dstnull ? NULL : a, P[i].cch); ea = GetLastError();
        SetLastError(0xD1CE); rb = wia_mbtwc(CP_UTF8, P[i].fl, P[i].srcnull ? NULL : src, P[i].cb, P[i].dstnull ? NULL : b, P[i].cch); eb = GetLastError();
        SetLastError(0xD1CE); rc = ref_mbtwc(CP_UTF8, P[i].fl, P[i].srcnull ? NULL : src, P[i].cb, P[i].dstnull ? NULL : c, P[i].cch); ec = GetLastError();
        if (ra != rb || rb != rc || ea != eb || eb != ec) bad = 1;
        for (j = 0; j < 64 && !bad; ++j) if (a[j] != b[j] || b[j] != c[j]) bad = 1;
        if (bad) { printf("FAIL param %-16s live %d/%lu  ours %d/%lu  ref %d/%lu\n", P[i].tag,
                          ra, (unsigned long)ea, rb, (unsigned long)eb, rc, (unsigned long)ec); ++failures; }
    }
    /* the alias rule: EXACT pointer equality, and only when cchWideChar != 0 */
    {
        static wchar_t z[64], y[64], x[64];
        struct { const char* tag; int off; int cch; } A[] = {
            { "src == dst",        0, 8 }, { "src == dst, cch=0", 0, 0 },
            { "src == dst+1",      1, 8 }, { "src == dst+6",      6, 8 },
        };
        int k;
        for (k = 0; k < 4; ++k) {
            int ra, rb, rc; DWORD ea, eb, ec;
            ++cases;
            memcpy(z, "abcdefgh", 8); memcpy(y, "abcdefgh", 8); memcpy(x, "abcdefgh", 8);
            SetLastError(0xD1CE); ra = sys(CP_UTF8, 0, ((const char*)z) + A[k].off, 3, z, A[k].cch); ea = GetLastError();
            SetLastError(0xD1CE); rb = wia_mbtwc(CP_UTF8, 0, ((const char*)y) + A[k].off, 3, y, A[k].cch); eb = GetLastError();
            SetLastError(0xD1CE); rc = ref_mbtwc(CP_UTF8, 0, ((const char*)x) + A[k].off, 3, x, A[k].cch); ec = GetLastError();
            if (ra != rb || rb != rc || ea != eb || eb != ec) {
                printf("FAIL alias %-20s live %d/%lu ours %d/%lu ref %d/%lu\n", A[k].tag,
                       ra, (unsigned long)ea, rb, (unsigned long)eb, rc, (unsigned long)ec);
                ++failures;
            }
        }
    }
}

/* every code page that is NOT 65001 must be indistinguishable: we tail-call the shipped export */
static void dispatch_boundary(void)
{
    static const UINT CP[] = { 0, 1, 2, 3, 437, 850, 932, 936, 949, 950, 1200, 1201, 1250, 1252,
                               10000, 12000, 12001, 20127, 28591, 50220, 51932, 54936, 57002,
                               65000, 65002, 60000, 99999, 0xFFFFFFFFu };
    static const unsigned char S[] = { 'a','b','c',0xC3,0xA9,0xE4,0xB8,0x80,0xFF,0x80 };
    static wchar_t a[64], b[64];
    int i, j, cch, wrong = 0;
    DWORD fl;
    /* The first call for a code page is not the same as the second: it loads that code page's NLS
       table, and that one-time work leaves the last error at 0 where a warm call leaves it alone.
       cp 50220 (ISO-2022-JP) showed it.  Both sides must therefore be measured warm. */
    for (i = 0; i < (int)(sizeof CP / sizeof CP[0]); ++i)
        for (j = 0; j < 2; ++j) { SetLastError(0); sys(CP[i], 0, (const char*)S, sizeof S, a, 64); }
    for (i = 0; i < (int)(sizeof CP / sizeof CP[0]); ++i)
        for (fl = 0; fl <= 8; fl += 8)
            for (cch = 0; cch <= 16; cch += 4) {
                int ra, rb; DWORD ea, eb, differ = 0;
                ++cases;
                for (j = 0; j < 64; ++j) a[j] = b[j] = (wchar_t)FILLW;
                SetLastError(0xD1CE); ra = sys(CP[i], fl, (const char*)S, sizeof S, cch ? a : NULL, cch); ea = GetLastError();
                SetLastError(0xD1CE); rb = wia_mbtwc(CP[i], fl, (const char*)S, sizeof S, cch ? b : NULL, cch); eb = GetLastError();
                for (j = 0; j < 64; ++j) if (a[j] != b[j]) differ = 1;
                if (ra != rb || ea != eb || differ) {
                    if (wrong < 8) printf("FAIL dispatch cp=%u fl=%lu cch=%d: live %d/%lu ours %d/%lu\n",
                                          CP[i], (unsigned long)fl, cch, ra, (unsigned long)ea, rb, (unsigned long)eb);
                    ++wrong; ++failures;
                }
            }
    printf("  dispatch boundary: %d disagreements over %d code pages\n", wrong,
           (int)(sizeof CP / sizeof CP[0]));
}

/* --------------------------------------------------------------------------------------------- */
int main(void)
{
    static unsigned char s[1024];
    int k, n, cch, i, q;
    unsigned st;
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");

    sys = (FN)GetProcAddress(k32, "MultiByteToWideChar");
    if (!sys) { printf("CORRECTNESS: cannot resolve MultiByteToWideChar\n"); return 1; }
    wia_mbtwc_set_fallback((void*)sys);

    printf("== assembler-generated tables ==\n");
    table_checks();

    printf("== parameters, flags, aliasing ==\n");
    parameter_matrix();

    printf("== dispatch boundary (every code page that is not CP_UTF8) ==\n");
    dispatch_boundary();

    printf("== twelve input classes x every length 0..200 x every capacity 0..2n ==\n");
    for (k = 0; k < NCLASS; ++k) {
        for (n = 0; n <= 200; ++n) {
            if (n) build(k, s, n);
            for (cch = 0; cch <= 2 * n + 2; ++cch) {
                if (n == 0) { one(s, 0, -1, cch, 0); continue; }   /* cb==0 is an error case */
                one(s, n, n, cch, 0);
                if ((cch & 7) == 0) one(s, n, n, cch, MB_ERR_INVALID_CHARS);
            }
            if (n) { s[n] = 0; one(s, n, -1, 512, 0); one(s, n, -1, 0, 0); }
        }
        printf("  class %-14s cases=%lld failures=%lld\n", KNAME[k], cases, failures);
        fflush(stdout);
    }

    printf("== the same classes with a malformed byte planted at every position ==\n");
    {
        static const unsigned char BADB[8] = { 0x80, 0xC0, 0xC1, 0xE0, 0xED, 0xF4, 0xF5, 0xFF };
        for (k = 0; k < NCLASS; ++k) {
            for (n = 1; n <= 70; ++n) {
                build(k, s, n);
                for (i = 0; i < n; ++i) {
                    unsigned char save = s[i];
                    for (q = 0; q < 8; ++q) {
                        s[i] = BADB[q];
                        one(s, n, n, 512, 0);
                        one(s, n, n, 512, MB_ERR_INVALID_CHARS);
                        one(s, n, n, 0, 0);
                        one(s, n, n, 0, MB_ERR_INVALID_CHARS);
                        one(s, n, n, n / 2, 0);
                        one(s, n, n, n, 0);
                    }
                    s[i] = save;
                }
            }
            printf("  class %-14s cases=%lld failures=%lld\n", KNAME[k], cases, failures);
            fflush(stdout);
        }
    }

    printf("== the boundary leads the blocks deliberately decline ==\n");
    {
        static const unsigned char LEADS[6] = { 0xE0, 0xED, 0xF0, 0xF4, 0xC2, 0xDF };
        int b2;
        for (k = 0; k < 6; ++k)
            for (b2 = 0; b2 < 256; ++b2)
                for (n = 1; n <= 40; ++n) {
                    int j = 0;
                    while (j < n) {
                        s[j++] = LEADS[k];
                        if (j < n) s[j++] = (unsigned char)b2;
                        if (j < n) s[j++] = 0x80;
                        if (LEADS[k] >= 0xF0 && j < n) s[j++] = 0x80;
                    }
                    one(s, n, n, 512, 0);
                    one(s, n, n, 0, 0);
                    one(s, n, n, n / 3, 0);
                }
        printf("  cases=%lld failures=%lld\n", cases, failures);
    }

    printf("== page guards ==\n");
    source_page_guard();
    dest_page_guard();

    printf("== randomised fuzz, fixed seed ==\n");
    st = 0x51ED;
    for (i = 0; i < 200000; ++i) {
        int len, c2; DWORD fl;
        st = st * 1103515245u + 12345u; len = (int)((st >> 8) % 500u) + 1;
        for (q = 0; q < len; ++q) { st = st * 1103515245u + 12345u; s[q] = (unsigned char)(st >> 17); }
        st = st * 1103515245u + 12345u; c2 = (int)((st >> 8) % (unsigned)(len + 3));
        st = st * 1103515245u + 12345u; fl = ((st >> 8) & 1u) ? MB_ERR_INVALID_CHARS : 0u;
        one(s, len, len, c2, fl);
        if ((i & 3) == 0) one(s, len, len, 0, fl);
    }
    printf("  cases=%lld failures=%lld\n", cases, failures);

    printf("== randomised WELL-FORMED utf-8, fixed seed ==\n");
    st = 0x9E37;
    for (i = 0; i < 120000; ++i) {
        int len = 0, c2, cut;
        while (len < 400) {
            unsigned r;
            st = st * 1103515245u + 12345u; r = (st >> 9) & 3u;
            if (r == 0) s[len++] = (unsigned char)('a' + (st % 26u));
            else if (r == 1) put2(s, &len, 1024, (int)(st % 64u));
            else if (r == 2) put3(s, &len, 1024, (int)(st % 64u));
            else put4(s, &len, 1024, (int)(st % 64u));
        }
        st = st * 1103515245u + 12345u; cut = (int)((st >> 8) % (unsigned)len) + 1;
        st = st * 1103515245u + 12345u; c2 = (int)((st >> 8) % 500u);
        one(s, cut, cut, c2, 0);
        if ((i & 3) == 0) one(s, cut, cut, 0, MB_ERR_INVALID_CHARS);
    }
    printf("  cases=%lld failures=%lld\n", cases, failures);

    printf("\nCORRECTNESS: %s  (%lld cases, %lld failures)\n",
           failures ? "FAIL" : "PASS", cases, failures);
    return failures != 0;
}
