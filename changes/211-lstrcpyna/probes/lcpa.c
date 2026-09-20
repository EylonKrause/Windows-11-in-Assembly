/* changes/211-lstrcpyna/probes/lcpa.c
   Pin down kernelbase!lstrcpynA before writing any assembly.

   Why: 1.69 GB/s to copy 4000 characters -- and 4000 NARROW characters take the same ~2375 ns that
   4000 WIDE ones take, i.e. one per-character loop regardless of width, so the narrow form has the
   larger ratio available. Change 209 took the wide one to 42.65 GB/s.

   The contract is NOT assumed to mirror 209's just because the names pair up. The A/W pairs in this
   project have gone both ways: change 203 inherited 202's contract exactly (0 differences over
   200 000 pairs), while change 205 turned out to REJECT the braces ntdll's parser requires. So every
   fact 209's loop shape rests on gets re-measured here against the narrow export:

     * how many characters are copied for a given iMaxLength, and where the NUL lands;
     * iMaxLength == 0, 1, and negative (is n used unsigned?);
     * the return value;
     * terminated only, or padded strncpy-style;
     * NULL source / NULL destination;
     * does it swallow a faulting source (SEH in the contract), and
     * The ordering question: does it read the source before testing the bound? 209's loop is shaped
       the way it is because the shipped code reads one character past the last one it copies, so a
       source that ends exactly at a guard page faults at n == srclen+1 -- a case where a
       bound-first loop would quietly succeed and DIFFER from Windows.

   And one question the wide form does not have: the ANSI code page. lstrcpynA on a DBCS code page
   could refuse to split a double-byte character at the truncation point. Measured, not assumed.    */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef char* (WINAPI *FN)(char*, const char*, int);
static FN cpn;

#define PB '*'
#define DSZ 32

static void show(const char* src, int n, const char* tag){
    char d[DSZ];
    for (int i = 0; i < DSZ; ++i) d[i] = PB;
    char* r = cpn(d, src, n);
    printf("  %-30s n=%-6d ret=%-7s buf=[", tag, n, r == d ? "dst" : (r ? "other" : "NULL"));
    for (int i = 0; i < 16; ++i) {
        char c = d[i];
        putchar(c == 0 ? '.' : (c == PB ? '-' : c));
    }
    printf("]\n");
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    cpn = (FN)GetProcAddress(h, "lstrcpynA");
    if (!cpn) { h = LoadLibraryW(L"kernel32.dll"); cpn = (FN)GetProcAddress(h, "lstrcpynA"); }
    if (!cpn) { printf("no lstrcpynA\n"); return 1; }
    printf("lstrcpynA = %p\n\n", (void*)cpn);

    printf("=== how many characters, and where does the NUL land? ('.'=NUL '-'=untouched) ===\n");
    show("abcdefgh", 16, "src 8, n 16 (room to spare)");
    show("abcdefgh",  9, "src 8, n 9  (exact fit)");
    show("abcdefgh",  8, "src 8, n 8  (one short)");
    show("abcdefgh",  5, "src 8, n 5");
    show("abcdefgh",  2, "src 8, n 2");
    show("abcdefgh",  1, "src 8, n 1");
    show("abcdefgh",  0, "src 8, n 0");
    show("abcdefgh", -1, "src 8, n -1");
    show("abcdefgh", -1000, "src 8, n -1000");
    show("",         4, "empty source");

    printf("\n=== is the destination PADDED (strncpy style) or just terminated? ===\n");
    {
        char d[DSZ];
        for (int i = 0; i < DSZ; ++i) d[i] = PB;
        cpn(d, "ab", 10);
        printf("  after copying \"ab\" with n=10, cells 0..9: ");
        for (int i = 0; i < 10; ++i) printf("%s", d[i] == 0 ? "." : (d[i] == PB ? "-" : "x"));
        printf("   (\"..-------\" = terminated only, \"..........\" = padded)\n");
    }

    printf("\n=== NULL arguments ===\n");
    {
        char d[DSZ];
        for (int i = 0; i < DSZ; ++i) d[i] = PB;
        char* r = cpn(d, NULL, 8);
        printf("  NULL source:      ret=%s  dst[0]=%02X\n",
               r == d ? "dst" : (r ? "other" : "NULL"), (unsigned char)d[0]);
        r = cpn(NULL, "abc", 8);
        printf("  NULL destination: ret=%s\n", r ? "non-NULL" : "NULL");
        r = cpn(NULL, NULL, 8);
        printf("  both NULL:        ret=%s\n", r ? "non-NULL" : "NULL");
    }

    printf("\n=== the ANSI code page: could a DBCS lead byte change where it truncates? ===\n");
    {
        UINT acp = GetACP();
        int leads = 0;
        for (int b = 0; b < 256; ++b) if (IsDBCSLeadByteEx(acp, (BYTE)b)) ++leads;
        printf("  GetACP() = %u,  lead bytes in this code page = %d\n", acp, leads);
        if (leads == 0)
            printf("  => no byte can start a double-byte character here, so a DBCS-aware truncation\n"
                   "     rule is unobservable on this machine and a byte-wise copy cannot differ.\n");
        else {
            char d[DSZ];
            char src[8];
            int lead = 0;
            for (int b = 0; b < 256; ++b) if (IsDBCSLeadByteEx(acp, (BYTE)b)) { lead = b; break; }
            src[0] = 'a'; src[1] = (char)lead; src[2] = 0x41; src[3] = 0;
            for (int i = 0; i < DSZ; ++i) d[i] = PB;
            cpn(d, src, 3);     /* n=3 -> 2 chars + NUL, cutting the DBCS pair in half */
            printf("  src = 'a' %02X 41, n=3 -> dst = %02X %02X %02X\n",
                   (unsigned char)lead, (unsigned char)d[0], (unsigned char)d[1], (unsigned char)d[2]);
            printf("  (lead byte kept at index 1 = split anyway/byte-wise; 00 at index 1 = refuses)\n");
        }
    }

    printf("\n=== THE DECIDING TEST: does it swallow a faulting source? ===\n");
    printf("MSDN says lstrcpyn catches exceptions and returns NULL. If so, a source that runs into\n");
    printf("an unmapped page is part of the contract and a plain vectorised copy cannot reproduce it.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);

        /* an UNTERMINATED source ending exactly at the guard page: reading past it must fault */
        char* s = (base + pg) - 8;
        for (int i = 0; i < 8; ++i) s[i] = (char)('a' + i);      /* no NUL anywhere */
        static char d[64];
        for (int i = 0; i < 64; ++i) d[i] = PB;

        printf("  calling with an 8-char unterminated source at a guard page, n=32 ...\n");
        char* r = cpn(d, s, 32);
        printf("  SURVIVED. ret=%s, dst=[", r == d ? "dst" : (r ? "other" : "NULL"));
        for (int i = 0; i < 12; ++i) {
            char c = d[i];
            putchar(c == 0 ? '.' : (c == PB ? '-' : c));
        }
        printf("]\n");

        printf("\n  === THE ORDERING QUESTION: read-then-bound, or bound-then-read? ===\n");
        printf("  Same 8-char unterminated source at the guard page, but n walks 1..10. A loop that\n");
        printf("  tests the bound FIRST never reads past what it copies and survives every n. The\n");
        printf("  shipped WIDE form reads first, so it dies one early -- at n == srclen+1.\n");
        for (int n = 1; n <= 10; ++n) {
            for (int i = 0; i < 64; ++i) d[i] = PB;
            char* rr = cpn(d, s, n);
            int copied = 0;
            while (copied < 63 && d[copied] != PB && d[copied] != 0) ++copied;
            printf("    n=%-3d ret=%-5s copied=%d%s\n", n,
                   rr ? "dst" : "NULL", copied,
                   rr ? "" : "   <- FAULTED: it read past the last character it copied");
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }
    return 0;
}
