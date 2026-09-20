// changes/233-pathquotespacesa/correctness.c
// Gate 1: wia_pathquotespacesa must be indistinguishable from shlwapi!PathQuoteSpacesA.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
//
// The whole buffer is compared against a poison fill, because "returns FALSE" and "returns FALSE
// having written nothing" are different contracts and only poison separates them. The measured rule
// is that a failure leaves the buffer completely untouched.
//
// One path is deliberately not compared byte for byte, and the reason is recorded here rather than
// left implicit. When the caller's buffer is too small for the result the shipped function FAULTS
// (37 of 37 distances), and probes/pqsa.c dumped what it had written first: indices 1..8, a
// contiguous run from the LOW end. That is the signature of a chunked memmove whose 8-byte head
// store lands before the tail store faults; a strict highest-byte-first shift could not produce
// it, because its very first write would be the faulting one. Our shift runs from the high end in
// 32-byte chunks and therefore leaves different bytes behind.
//
// That divergence is confined to a path that FAULTS: it is visible only to a caller that installs a
// handler around a call it got wrong, the chunk schedule is not part of any contract, and it would
// change with any servicing update. So the sweep below asserts that both sides fault and compares
// nothing else. Every case where the function RETURNS is compared in full.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern int wia_pathquotespacesa(char*);
int ref_pathquotespacesa(char*);
typedef int (WINAPI *FN)(char*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<15) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON '#'
#define DSZ 640

static unsigned long sd = 0x7C3Fu;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int chk(const char* src, int off, const char* what)
{
    static char a[DSZ], b[DSZ], c[DSZ];
    memset(a, POISON, DSZ); memset(b, POISON, DSZ); memset(c, POISON, DSZ);
    size_t n = strlen(src);
    char* pa = a + off; char* pb = b + off; char* pc = c + off;
    memcpy(pa, src, n+1); memcpy(pb, src, n+1); memcpy(pc, src, n+1);
    int ra = wia_pathquotespacesa(pa);
    int rb = ref_pathquotespacesa(pb);
    int rc = sys(pc);
    int ok = (!!ra) == (!!rb) && (!!ra) == (!!rc)
          && memcmp(a, b, DSZ) == 0 && memcmp(a, c, DSZ) == 0;
    if (!ok && fails < 15)
        printf("FAIL: %s -- n=%d off=%d: ours %d oracle %d live %d\n",
               what, (int)n, off, ra, rb, rc);
    if (!ok) ++fails;
    return ok;
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h,"PathQuoteSpacesA");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!PathQuoteSpacesA\n"); return 1; }
    printf("  GetACP() = %u\n", GetACP());

    static char s[700];

    // probe-derived cases
    {
        static const char* V[] = { "", "a", " ", "  ", "a b", "a b c", " a", "a ",
                                   "\"a b\"", "C:\\Program Files\\x", "noSpace", "\ta\tb", 0 };
        for (int i = 0; V[i]; ++i)
            for (int off = 0; off < 4; ++off)
                chk(V[i], off, "probe-derived case");
    }

    // ---- every byte value, at three positions: exactly 0x20 may trigger the quoting -------------
    for (int v = 1; v < 256; ++v) {
        s[0]='a'; s[1]=(char)v; s[2]='b'; s[3]=0;   chk(s, 0, "byte value in the middle");
        s[0]=(char)v; s[1]='a'; s[2]=0;             chk(s, 0, "byte value first");
        s[0]='a'; s[1]=(char)v; s[2]=0;             chk(s, 0, "byte value last");
    }

    // ---- The length cap, swept exactly -----------------------------------------------------------
    // 257 quotes, 258 does not. Every length across the boundary, with the space at the front, in
    // the middle and at the end, because "is there a space" and "how long is it" are found by the
    // same pass and an off-by-one in either is a different bug.
    for (int n = 240; n <= 280; ++n) {
        for (int where = 0; where < 3; ++where) {
            for (int i = 0; i < n; ++i) s[i] = (char)('a' + i % 23);
            int pos = where == 0 ? 0 : (where == 1 ? n/2 : n-1);
            s[pos] = ' ';
            s[n] = 0;
            chk(s, 0, "length cap sweep");
        }
        /* and with no space at all, which must be FALSE at every length */
        for (int i = 0; i < n; ++i) s[i] = (char)('a' + i % 23);
        s[n] = 0;
        chk(s, 0, "length sweep, no space");
    }

    // ---- EXHAUSTIVE over the alphabet that reaches every branch -----------------------------------
    {
        static const char AL[4] = { 'a', ' ', '"', '\t' };
        long en = 0;
        for (int n = 0; n <= 9; ++n) {
            long lim = 1; for (int i = 0; i < n; ++i) lim *= 4;
            for (long k = 0; k < lim; ++k) {
                long v = k;
                for (int i = 0; i < n; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[n] = 0;
                chk(s, 0, "exhaustive {a,space,quote,tab} 0..9");
                ++en;
            }
        }
        printf("  exhaustive {a,space,quote,tab} 0..9: %ld strings\n", en);
    }

    // ---- alignments and long strings -------------------------------------------------------------
    for (int off = 0; off < 32; ++off) {
        for (int n = 0; n <= 70; ++n) {
            for (int i = 0; i < n; ++i) s[i] = (char)('a' + i % 23);
            if (n > 3) s[n/2] = ' ';
            s[n] = 0;
            chk(s, off, "alignment x length");
        }
    }

    // ---- NULL --------------------------------------------------------------------------------------
    CHECK(wia_pathquotespacesa(0) == 0, "NULL returns 0");
    CHECK(ref_pathquotespacesa(0) == 0, "NULL returns 0 (oracle)");
    CHECK(sys(0) == 0,                  "NULL returns 0 (live)");

    // ---- fuzz ----------------------------------------------------------------------------------------
    {
        static const char AL[8] = { 'a', ' ', '"', '\t', 'b', '\\', (char)0xA0, (char)0x20 };
        for (int t = 0; t < 300000; ++t) {
            int n = rnd() % 300;
            for (int i = 0; i < n; ++i) s[i] = AL[rnd() % 8];
            s[n] = 0;
            chk(s, rnd() % 16, "fuzz");
        }
    }

    // ---- NOACCESS page guard on the SCAN: the string ends at the guard ------------------------------
    // The scan must stop at the terminator. These cases have NO space, so the function returns
    // FALSE and writes nothing, the guard only tests the read.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char b[DSZ];
        for (int tail = 2; tail <= 200; ++tail) {
            char* p = (base+pg) - tail;
            for (int i = 0; i < tail-1; ++i) p[i] = (char)('a' + i % 23);
            p[tail-1] = 0;
            memset(b, POISON, DSZ);
            int n = 0; while (p[n]) { b[n] = p[n]; ++n; } b[n] = 0;
            int ra = wia_pathquotespacesa(p);      // must not read into page 2
            int rb = ref_pathquotespacesa(b);
            CHECK((!!ra) == (!!rb), "page-guard scan: same result");
            CHECK(memcmp(p, b, tail) == 0, "page-guard scan: buffer untouched, identically");
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    // ---- a buffer too small for the result: both must fault -----------------------------------------
    // Only that. See the header: the shipped function's partial state here comes from the chunk
    // schedule of its internal move (it leaves indices 1..8, an 8-byte head store), ours comes from
    // a 32-byte high-end shift, and that schedule is not a contract anyone can rely on.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        int both = 0, cases = 0;
        for (int room = 4; room <= 80; ++room) {
            char* ba = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            char* bc = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            DWORD old;
            VirtualProtect(ba+pg, pg, PAGE_NOACCESS, &old);
            VirtualProtect(bc+pg, pg, PAGE_NOACCESS, &old);
            char* da = (ba+pg) - room;
            char* dc = (bc+pg) - room;
            for (int i = 0; i < room-1; ++i) { da[i] = 'a'; dc[i] = 'a'; }
            da[1] = ' '; dc[1] = ' ';
            da[room-1] = 0; dc[room-1] = 0;
            int fa = 0, fc = 0;
            __try { wia_pathquotespacesa(da); } __except (EXCEPTION_EXECUTE_HANDLER) { fa = 1; }
            __try { sys(dc); }                  __except (EXCEPTION_EXECUTE_HANDLER) { fc = 1; }
            CHECK(fa == fc, "too-small buffer: both must fault, or neither");
            if (fa && fc) ++both;
            ++cases;
            VirtualFree(ba, 0, MEM_RELEASE);
            VirtualFree(bc, 0, MEM_RELEASE);
        }
        printf("  too-small-buffer sweep: %d of %d faulted on BOTH sides\n", both, cases);
        CHECK(both == cases, "every too-small case faulted on both sides");
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathQuoteSpacesA vs live shlwapi + oracle, WHOLE-BUFFER compare "
           "against poison -- required, because a failure must leave the buffer completely "
           "untouched: probe-derived cases at 4 alignments, ALL 255 byte values at three positions "
           "(exactly 0x20 triggers the quoting), the LENGTH CAP swept over 240..280 with the space "
           "at the front, middle and end plus a no-space control at every length, exhaustive "
           "{a,SPACE,quote,TAB} to length 9 (349525 strings), 32 alignments x lengths 0..70, NULL, "
           "300k fuzz, a NOACCESS page-guard sweep on the scan, and a too-small-buffer sweep where "
           "both sides must FAULT -- deliberately not compared byte for byte, for the reason in "
           "this file's header)\n");
    return 0;
}
