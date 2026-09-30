// changes/301-rtlisnameinunupcasedexpression/correctness.c
// Gate 1: wia_nameinexpr against the live ntdll!RtlIsNameInUnUpcasedExpression (IgnoreCase = FALSE)
// and against reference.c.
//
// The algorithm was proven in C first (probes/model_check.c, 1,441,656 cases), so this gate is
// aimed at what the TRANSLITERATION can get wrong, and each pass targets one failure class:
//
//   * exhaustive   every pattern of length 0..4 over {a . * ? < > "} against every name of length
//                  0..5 over {a b .}: 1,019,564 cases, every branch of every wildcard.
//   * random       patterns up to 40 and names up to 700, so the greedy path's star backtracking,
//                  the AVX2 skip and the column DP's carries cross many 16-wchar blocks and 64-bit
//                  words.
//   * word edges   names of every length 1..700 against the DOS shapes, because the column DP's
//                  last word is partial on 63 of every 64 lengths and a scalar tail that is off by
//                  one disagrees only there.
//   * 32767 chars  the longest name a UNICODE_STRING can carry, 513 DP words: the dynamic stack
//                  allocation and its page probes run only on inputs this large.
//   * odd Length   a byte count that is not a multiple of two. The export does NOT truncate it:
//                  it walks by byte offset while offset < Length, so the count is ceil(Length/2).
//   * alignment    the name buffer at every even offset 0..62, so no load relies on alignment.
//   * page guard   the pattern and the name each placed so the byte after their last wchar is
//                  NOACCESS, at every length 1..200: a single wchar of over-read dies at once.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef BOOLEAN (NTAPI *PFN_WILD)(const USTR*, const USTR*, BOOLEAN, PWCH);

extern BOOLEAN wia_nameinexpr(const USTR*, const USTR*, BOOLEAN, PWCH);
int ref_name_in_expression(const unsigned short*, int, const unsigned short*, int);

static PFN_WILD sys;
static long long tested, fails;

static void chk_raw(const wchar_t* p, USHORT pbytes, const wchar_t* n, USHORT nbytes, const char* tag) {
    USTR e = { pbytes, pbytes, (PWSTR)p };
    USTR m = { nbytes, nbytes, (PWSTR)n };
    int s = sys(&e, &m, FALSE, NULL) ? 1 : 0;
    int o = wia_nameinexpr(&e, &m, FALSE, NULL) ? 1 : 0;
    int r = s;
    if (((pbytes + 1) / 2) <= 512 && ((nbytes + 1) / 2) <= 8192)
        r = ref_name_in_expression((const unsigned short*)p, pbytes, (const unsigned short*)n, nbytes);
    ++tested;
    if (s != o || s != r) {
        if (fails < 15)
            printf("FAIL [%s] pl=%u nl=%u pat=\"%.*ls\" name=\"%.*ls\" sys=%d ours=%d ref=%d\n", tag,
                   pbytes / 2, nbytes / 2, pbytes / 2 > 40 ? 40 : pbytes / 2, p, nbytes / 2 > 60 ? 60 : nbytes / 2, n, s, o, r);
        ++fails;
    }
}
static void chk(const wchar_t* p, int pl, const wchar_t* n, int nl, const char* tag) {
    chk_raw(p, (USHORT)(pl * 2), n, (USHORT)(nl * 2), tag);
}

static const wchar_t* SHAPES[] = {
    L"*", L"*.txt", L"*.zzz", L"file*", L"zzzz*", L"*.*", L"????", L"*abc*def*", L"*q*", L"<.txt",
    L"<\"*", L"<", L"*.>>>", L"a<b<c<d", L"f<", L"f*t", L"*t", L"<t", L"*>", L">*", L"\"*", L"*\"",
    L"*.<", L"<.<", L"f?le*", L"*?*?*", L"<<<<", L">>>>", L"*.t>t", L"f\"*", L"file.txt", L"*le.t*",
};
#define NSH ((int)(sizeof SHAPES / sizeof SHAPES[0]))

static void mkname(wchar_t* nam, int nl, int dots) {
    for (int j = 0; j < nl; ++j) nam[j] = (wchar_t)(L'a' + (j * 7 % 26));
    if (dots >= 1 && nl >= 5) { nam[nl-4] = L'.'; nam[nl-3] = L't'; nam[nl-2] = L'x'; nam[nl-1] = L't'; }
    if (dots >= 2) { for (int j = 13; j < nl; j += 97) nam[j] = L'.'; if (nl > 1) nam[nl-1] = L'.'; }
    if (nl > 4) { nam[0] = L'f'; nam[1] = L'i'; nam[2] = L'l'; nam[3] = L'e'; }
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    sys = (PFN_WILD)GetProcAddress(LoadLibraryW(L"ntdll.dll"), "RtlIsNameInUnUpcasedExpression");
    if (!sys) { printf("no RtlIsNameInUnUpcasedExpression\n"); return 2; }

    /* 1. exhaustive */
    {
        static const wchar_t PA[] = { L'a', L'.', L'*', L'?', L'<', L'>', L'"' };
        static const wchar_t NA[] = { L'a', L'b', L'.' };
        wchar_t pat[10], nam[10];
        for (int pl = 0; pl <= 4; ++pl) {
            long long pc_n = 1; for (int i = 0; i < pl; ++i) pc_n *= 7;
            for (long long pc = 0; pc < pc_n; ++pc) {
                long long t = pc;
                for (int i = 0; i < pl; ++i) { pat[i] = PA[t % 7]; t /= 7; }
                for (int nl = 0; nl <= 5; ++nl) {
                    long long nc_n = 1; for (int i = 0; i < nl; ++i) nc_n *= 3;
                    for (long long nc = 0; nc < nc_n; ++nc) {
                        long long u = nc;
                        for (int i = 0; i < nl; ++i) { nam[i] = NA[u % 3]; u /= 3; }
                        chk(pat, pl, nam, nl, "exhaustive");
                    }
                }
            }
        }
        printf("  exhaustive:          %lld cases, %lld fails\n", tested, fails);
    }

    /* 2. random, long */
    {
        static wchar_t pat[64], nam[800];
        static const wchar_t PA[] = { L'a', L'b', L'.', L'*', L'?', L'<', L'>', L'"' };
        static const wchar_t NA[] = { L'a', L'b', L'.' };
        unsigned long seed = 0x30120u;
        long long t0 = tested, f0 = fails;
        for (int t = 0; t < 400000 && fails < 15; ++t) {
            seed = seed * 1103515245u + 12345u;
            int pl = (int)((seed >> 7) % 41);
            int nl = (int)((seed >> 13) % 701);
            int pure = (((seed >> 3) & 3) == 0);
            for (int i = 0; i < pl; ++i) { seed = seed * 1103515245u + 12345u; pat[i] = pure ? PA[(seed >> 8) % 5] : PA[(seed >> 8) % 8]; }
            for (int i = 0; i < nl; ++i) { seed = seed * 1103515245u + 12345u; nam[i] = NA[(seed >> 8) % 3]; }
            chk(pat, pl, nam, nl, "random");
        }
        printf("  random pl<=40 nl<=700: %lld cases, %lld fails\n", tested - t0, fails - f0);
    }

    /* 3. every name length 1..700 (and sparser to 4100) against every shape, three dot layouts */
    {
        static wchar_t nam[4200];
        long long t0 = tested, f0 = fails;
        for (int i = 0; i < NSH && fails < 15; ++i) {
            int pl = (int)wcslen(SHAPES[i]);
            for (int nl = 1; nl <= 4100; nl += (nl < 700 ? 1 : 67))
                for (int dots = 0; dots < 3; ++dots) { mkname(nam, nl, dots); chk(SHAPES[i], pl, nam, nl, "shapes"); }
        }
        printf("  shapes x lengths:    %lld cases, %lld fails\n", tested - t0, fails - f0);
    }

    /* 4. the longest name a UNICODE_STRING can hold: 32767 wchars, 513 DP words, page probes */
    {
        static wchar_t nam[32768];
        long long t0 = tested, f0 = fails;
        int lens[] = { 32767, 32766, 32704, 32703, 16384, 8193, 4097 };
        for (int k = 0; k < 7; ++k)
            for (int dots = 0; dots < 3; ++dots) {
                mkname(nam, lens[k], dots);
                for (int i = 0; i < NSH; ++i) chk(SHAPES[i], (int)wcslen(SHAPES[i]), nam, lens[k], "32767");
            }
        printf("  names up to 32767:   %lld cases, %lld fails\n", tested - t0, fails - f0);
    }

    /* 5. odd byte lengths: the export divides by two, and so must we */
    {
        static wchar_t nam[64];
        long long t0 = tested, f0 = fails;
        for (int i = 0; i < NSH; ++i) {
            int pl = (int)wcslen(SHAPES[i]);
            for (int nl = 0; nl < 40; ++nl) {
                mkname(nam, nl + 1, 1);
                chk_raw(SHAPES[i], (USHORT)(pl * 2 + 1), nam, (USHORT)(nl * 2), "odd-pat");
                chk_raw(SHAPES[i], (USHORT)(pl * 2), nam, (USHORT)(nl * 2 + 1), "odd-name");
            }
        }
        printf("  odd byte lengths:    %lld cases, %lld fails\n", tested - t0, fails - f0);
    }

    /* 6. every even buffer offset */
    {
        static wchar_t big[2200];
        long long t0 = tested, f0 = fails;
        for (int off = 0; off < 32; ++off)
            for (int nl = 1; nl <= 300; nl += 7) {
                wchar_t* nam = big + off;
                mkname(nam, nl, off & 1 ? 2 : 1);
                for (int i = 0; i < NSH; ++i) chk(SHAPES[i], (int)wcslen(SHAPES[i]), nam, nl, "align");
            }
        printf("  alignments 0..62 B:  %lld cases, %lld fails\n", tested - t0, fails - f0);
    }

    /* 7. NOACCESS page right after the last wchar of the name, then of the pattern */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        unsigned char* b = (unsigned char*)VirtualAlloc(NULL, pg * 4, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(b, pg * 2, MEM_COMMIT, PAGE_READWRITE);
        wchar_t* end = (wchar_t*)(b + pg * 2);
        static wchar_t tmp[400];
        long long t0 = tested, f0 = fails;
        for (int nl = 1; nl <= 200; ++nl)
            for (int dots = 0; dots < 3; ++dots) {
                mkname(tmp, nl, dots);
                wchar_t* nam = end - nl;
                memcpy(nam, tmp, nl * 2);
                for (int i = 0; i < NSH; ++i) chk(SHAPES[i], (int)wcslen(SHAPES[i]), nam, nl, "guard-name");
            }
        for (int i = 0; i < NSH; ++i) {
            int pl = (int)wcslen(SHAPES[i]);
            wchar_t* pat = end - pl;
            memcpy(pat, SHAPES[i], pl * 2);
            for (int nl = 1; nl <= 120; ++nl) { mkname(tmp, nl, nl % 3); chk(pat, pl, tmp, nl, "guard-pat"); }
        }
        printf("  NOACCESS guards:     %lld cases, %lld fails\n", tested - t0, fails - f0);
        VirtualFree(b, 0, MEM_RELEASE);
    }

    if (!fails)
        printf("CORRECTNESS: PASS (RtlIsNameInUnUpcasedExpression, IgnoreCase=FALSE, vs live + oracle: %lld cases -- exhaustive, random, every length to 700, 32767-wchar names, odd lengths, alignments, NOACCESS guards)\n", tested);
    else
        printf("CORRECTNESS: FAIL (%lld of %lld)\n", fails, tested);
    return fails ? 1 : 0;
}
