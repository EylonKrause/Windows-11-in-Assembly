// changes/225-lstrlena/correctness.c
// Gate 1: wia_lstrlena must be indistinguishable from kernelbase!lstrlenA.
// Three-way: our ASM+wrapper vs the scalar oracle vs the LIVE export on this PC.
//
// THE PAGE-GUARD SECTIONS ARE THE POINT OF THIS FILE, not an afterthought at the end. A 32-byte
// scan that reads one block too far is INVISIBLE to an ordinary length test -- every string in a
// heap buffer has slack after it, so the over-read lands on readable bytes and the answer is
// right. It only shows up when the string ends within 32 bytes of an unmapped page, and then it
// does not crash: the shipped export swallows the fault and returns 0, so a correct 3-character
// string would come back as 0. That is silent data corruption, and only a guard page finds it.
//
// So this sweeps EVERY tail distance from 1 to 200 bytes before a PAGE_NOACCESS page, twice: once
// with the string properly terminated (where the answer must be the length) and once with NO
// terminator at all (where the answer must be 0, matching the live export rather than crashing).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern int wia_lstrlena(const char*);
int ref_lstrlena(const char*);
typedef int (WINAPI *FN)(const char*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<15) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

static unsigned long sd = 0xBEEFu;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

/* one three-way case */
static int chk(const char* p, const char* what)
{
    int a = wia_lstrlena(p);
    int b = ref_lstrlena(p);
    int c = sys(p);
    int ok = (a == b) && (a == c);
    if (!ok && fails < 15)
        printf("FAIL: %s -- ours %d, oracle %d, live %d\n", what, a, b, c);
    if (!ok) ++fails;
    return ok;
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h,"lstrlenA");
    if(!sys){ printf("CORRECTNESS: cannot resolve kernelbase!lstrlenA\n"); return 1; }
    printf("  GetACP() = %u\n", GetACP());

    // ---- lengths 0..2048 at EVERY start offset within a 64-byte window -------------------------
    // 64 offsets, because the first block is aligned down to 32 and the paired loop realigns to
    // 64: the peel-one-block path only runs for half of them, and the masked-first-block path
    // behaves differently for each of the 32 residues.
    {
        static char buf[4096 + 128];
        for (int offs = 0; offs < 64; ++offs) {
            char* p = buf + offs;
            for (int n = 0; n <= 300; ++n) {
                for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 23);
                p[n] = 0;
                if (!chk(p, "offset x length 0..300")) { printf("   (offset %d, length %d)\n", offs, n); }
            }
            for (int n = 301; n <= 2048; n += 17) {
                for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 23);
                p[n] = 0;
                chk(p, "offset x length 301..2048");
            }
        }
    }

    // ---- EVERY byte value, at three positions ---------------------------------------------------
    // Only 0x00 may terminate. A lead byte immediately before the NUL is the placement that would
    // catch an MBCS-aware implementation swallowing the terminator.
    {
        static char b[64];
        for (int v = 1; v < 256; ++v) {
            b[0]='a'; b[1]='b'; b[2]=(char)v; b[3]='c'; b[4]='d'; b[5]=0;
            chk(b, "byte value in the middle");
            b[0]=(char)v; b[1]='b'; b[2]='c'; b[3]=0;
            chk(b, "byte value as the first byte");
            b[0]='a'; b[1]=(char)v; b[2]=0;
            chk(b, "byte value immediately before the NUL");
        }
    }
    // and a string made ENTIRELY of one byte value, long enough to drive the paired loop
    {
        static char b[512];
        for (int v = 1; v < 256; ++v) {
            for (int i = 0; i < 200; ++i) b[i] = (char)v;
            b[200] = 0;
            chk(b, "200 copies of one byte value");
        }
    }

    // ---- NULL -----------------------------------------------------------------------------------
    CHECK(wia_lstrlena(0) == 0, "NULL returns 0 (ours)");
    CHECK(ref_lstrlena(0) == 0, "NULL returns 0 (oracle)");
    CHECK(sys(0) == 0,          "NULL returns 0 (live)");

    // ---- a 1 MB string ---------------------------------------------------------------------------
    {
        char* big = (char*)VirtualAlloc(0, 1<<21, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(big != 0, "VirtualAlloc 2MB");
        if (big) {
            int n = 1 << 20;
            memset(big, 'x', (size_t)n);
            big[n] = 0;
            CHECK(wia_lstrlena(big) == n, "1MB string, exact");
            CHECK(sys(big) == n,          "1MB string, exact (live)");
            /* and unaligned, so the masked first block runs on a long subject */
            for (int off = 1; off < 64; ++off) {
                big[n] = 'x'; big[n+off] = 0;
                CHECK(wia_lstrlena(big+off) == n, "1MB string, unaligned start");
                big[n+off] = 'x';
            }
            VirtualFree(big, 0, MEM_RELEASE);
        }
    }

    // ---- PAGE GUARD, terminated: the answer must be the LENGTH -----------------------------------
    // Every tail distance 1..200. An implementation that reads one block past the terminator would
    // touch the guard, the wrapper would swallow it, and the length would come back 0.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != 0, "VirtualAlloc guard pair");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        for (int tail = 1; tail <= 200; ++tail) {
            char* p = (base+pg) - tail;
            for (int i = 0; i < tail-1; ++i) p[i] = (char)('a' + i % 23);
            p[tail-1] = 0;                        /* terminated exactly at the last readable byte */
            int a = wia_lstrlena(p);
            int c = sys(p);
            CHECK(a == tail-1, "page-guard, terminated: ours must be the length");
            CHECK(a == c,      "page-guard, terminated: ours must match the live export");
            if (a != tail-1 && fails < 16)
                printf("   (tail %d: ours %d, live %d, expected %d)\n", tail, a, c, tail-1);
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    // ---- PAGE GUARD, UNTERMINATED: the answer must be 0, and it must not crash -------------------
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != 0, "VirtualAlloc guard pair (unterminated)");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        for (int tail = 1; tail <= 200; ++tail) {
            char* p = (base+pg) - tail;
            for (int i = 0; i < tail; ++i) p[i] = (char)('a' + i % 23);   /* NO terminator */
            int a = wia_lstrlena(p);
            int c = sys(p);
            CHECK(a == 0, "page-guard, unterminated: must return 0");
            CHECK(a == c, "page-guard, unterminated: must match the live export");
            if (a != c && fails < 16) printf("   (tail %d: ours %d, live %d)\n", tail, a, c);
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    // ---- PAGE GUARD with the string STARTING at the page boundary --------------------------------
    // The first block is aligned DOWN, so for a string starting exactly at a page start the load
    // reaches backwards into the previous page. That page must be the one the caller's string is
    // in -- which it is, because the alignment is to 32 and 4096 is a multiple of 32, so aligning
    // down never leaves the page. This section proves it by making the PRECEDING page unreadable.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*3, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != 0, "VirtualAlloc guard triple");
        DWORD old;
        VirtualProtect(base, pg, PAGE_NOACCESS, &old);          /* the page BEFORE the string */
        VirtualProtect(base+pg*2, pg, PAGE_NOACCESS, &old);     /* and the page after it      */
        for (int n = 0; n <= 200; ++n) {
            char* p = base + pg;                                 /* exactly at a page start */
            for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 23);
            p[n] = 0;
            int a = wia_lstrlena(p);
            int c = sys(p);
            CHECK(a == n, "string at a page start, previous page unreadable");
            CHECK(a == c, "string at a page start: matches the live export");
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    // ---- randomized fuzz ------------------------------------------------------------------------
    {
        static char b[1200];
        for (int t = 0; t < 200000; ++t) {
            int n = rnd() % 1000;
            for (int i = 0; i < n; ++i) { int v = 1 + (int)(rnd() % 255); b[i] = (char)v; }
            b[n] = 0;
            int off = (int)(rnd() % 64);
            memmove(b+off, b, (size_t)n+1);
            chk(b+off, "fuzz");
        }
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (lstrlenA vs live kernelbase + oracle: 64 start offsets x lengths "
           "0..300 exhaustively and 301..2048 stepped, ALL 255 non-NUL byte values at three "
           "positions and as a 200-byte run, NULL, a 1MB string at 64 alignments, 200k fuzz, and "
           "THREE NOACCESS page-guard sweeps -- terminated at every tail 1..200 where the answer "
           "must be the LENGTH, unterminated at every tail 1..200 where it must be 0 without "
           "crashing, and the string starting exactly at a page boundary with the PRECEDING page "
           "unreadable, which is what the align-down first block has to survive)\n");
    return 0;
}
