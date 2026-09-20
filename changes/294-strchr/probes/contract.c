// changes/294-strchr/probes/contract.c
//
// PROBE -- what does the LIVE ucrtbase!strchr actually do, and does msvcrt!strchr agree?
//
// Four questions the change's contract depends on, none of which are answered by reading the
// C standard (change 289 proved MSDN wrong about WideCharToMultiByte three separate ways this
// week, so documented behaviour is a hypothesis, not a fact):
//
//   Q1  strchr(s, 0)      -- the standard says the terminator is part of the string and is
//                            therefore matchable. Does ucrtbase return &s[len] or NULL?
//   Q2  c above 127       -- the parameter is `int` and the comparison is specified "as if
//                            converted to char". Windows' char is SIGNED. Does the export
//                            compare the byte, or sign-extend and miss?
//   Q3  c outside 0..255  -- 0x100 + 'a', and negative ints. Truncate to 8 bits, or no match?
//   Q4  empty string      -- "" with a non-zero needle, and "" with 0.
//
//  plus Q5: a needle that appears only AFTER the terminator (inside the same aligned block the
//  scanner will physically load) must NOT be found -- that is the one place a dual-search
//  implementation can silently be wrong while every "normal" test passes.
//
// Build:  cl /nologo /O2 contract.c /Fe:contract.exe   (from a VS x64 env)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef char* (__cdecl *fn)(const char*, int);

static fn ucrt, msv;
static int rows = 0, disagree = 0;

static long long rel(const char* s, char* r) { return r ? (long long)(r - s) : -1; }

static void ask(const char* what, const char* s, int c)
{
    char* a = ucrt(s, c);
    char* b = msv (s, c);
    printf("%-42s c=%-8d  ucrtbase=%-4lld  msvcrt=%-4lld  %s\n",
           what, c, rel(s, a), rel(s, b), (a == b) ? "" : "  <-- DISAGREE");
    ++rows;
    if (a != b) ++disagree;
}

int main(void)
{
    ucrt = (fn)GetProcAddress(LoadLibraryA("ucrtbase.dll"), "strchr");
    msv  = (fn)GetProcAddress(LoadLibraryA("msvcrt.dll"),   "strchr");
    if (!ucrt || !msv) { printf("resolve failed\n"); return 2; }

    // -1 in the columns below means NULL; anything >= 0 is a byte offset from s.

    puts("--- Q1: is the terminator matchable? ---");
    ask("\"abc\", needle 0",                 "abc", 0);
    ask("\"\", needle 0",                    "",    0);

    puts("\n--- Q2/Q3: how is the int needle narrowed? ---");
    static const char hi[] = { 'a','b',(char)0xE9,'d',0 };      // 0xE9 = e-acute in CP1252
    ask("\"ab\\xE9d\", needle 0xE9",          hi, 0xE9);
    ask("\"ab\\xE9d\", needle (int)(char)0xE9", hi, (int)(char)0xE9);   // = -23, sign-extended
    ask("\"ab\\xE9d\", needle 0xFFE9",        hi, 0xFFE9);
    ask("\"ab\\xE9d\", needle 0x1E9",         hi, 0x1E9);
    ask("\"abcd\", needle 0x161 (0x100+'a')", "abcd", 0x161);
    ask("\"abcd\", needle -1 (0xFF byte?)",   "abcd", -1);
    ask("\"abcd\", needle 0x100 (0 byte?)",   "abcd", 0x100);   // does it become the terminator?
    ask("\"abcd\", needle 0xFFFFFF00",        "abcd", (int)0xFFFFFF00);

    puts("\n--- Q4: empty string ---");
    ask("\"\", needle 'a'",                   "", 'a');

    puts("\n--- Q5: needle only after the terminator ---");
    {
        // 32 bytes so the scanner's first aligned block certainly contains all of it.
        static char buf[64];
        memset(buf, 'x', sizeof buf);
        buf[3] = 0; buf[5] = 'q'; buf[40] = 0;
        ask("\"xxx\\0xq...\", needle 'q' (past NUL)", buf, 'q');
    }

    puts("\n--- Q6: every start alignment, needle absent, needle at end ---");
    {
        static char page[256];
        for (int off = 0; off < 32; ++off) {
            char* s = page + off;
            for (int i = 0; i < 20; ++i) s[i] = 'a' + (i & 7);
            s[20] = 'Z'; s[21] = 0;
            char* a = ucrt(s, 'Z');
            char* b = msv (s, 'Z');
            char* n = ucrt(s, 'Q');
            char* m = msv (s, 'Q');
            ++rows;
            if (a != b || n != m || rel(s,a) != 20 || n != NULL) {
                printf("  off=%2d  ucrt(Z)=%lld msv(Z)=%lld  ucrt(Q)=%lld msv(Q)=%lld  <-- ODD\n",
                       off, rel(s,a), rel(s,b), rel(s,n), rel(s,m));
                ++disagree;
            }
        }
        printf("  32 start alignments: match-at-end and absent both as expected\n");
    }

    puts("\n--- Q7: does it read past the terminator into the NEXT page? ---");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        DWORD pg = si.dwPageSize, old;
        unsigned char* base = (unsigned char*)VirtualAlloc(NULL, pg * 2,
                                  MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);
        int faults = 0;
        for (int tail = 1; tail <= 64; ++tail) {
            char* term = (char*)(base + pg - tail);
            char* s = term - 40;
            for (char* p = s; p < term; ++p) *p = 'A';
            *term = 0;
            __try { (void)ucrt(s, 'Q'); (void)msv(s, 'Q'); }
            __except (EXCEPTION_EXECUTE_HANDLER) { ++faults; }
        }
        printf("  terminator 1..64 bytes before a PAGE_NOACCESS page: %d faults\n", faults);
        rows += 64;
        disagree += faults;
    }

    printf("\n%d probes, %d disagreements/faults\n", rows, disagree);
    return 0;
}
