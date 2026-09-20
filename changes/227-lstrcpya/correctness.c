// changes/227-lstrcpya/correctness.c
// Gate 1: wia_lstrcpya must be indistinguishable from kernelbase!lstrcpyA.
// Three-way: our ASM + wrapper vs the scalar oracle vs the LIVE export on this PC.
//
// The two guard-page sections are the point of this file.
//
// lstrcpyA has NO bound. It always runs off the end of a destination too small for the source, and
// probes/cpya.c measured what that does: it returns NULL rather than faulting, and the destination
// is filled exactly to its last writable byte, 80 of 80 rooms. The same is true on the source
// side: an unterminated source at a guard page returns NULL with exactly the readable prefix
// transferred.
//
// An implementation that page-clamps only the SOURCE passes every ordinary test and then writes a
// whole 32-byte chunk into a destination the shipped function fills only partway. Nothing crashes;
// the return value is NULL either way; the difference is only in bytes the caller can still read.
// So both sides are swept at every distance, and the comparison is byte-for-byte against the live
// export rather than against the oracle, because the oracle cannot model a fault.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern char* wia_lstrcpya(char*, const char*);
char* ref_lstrcpya(char*, const char*);
typedef char* (WINAPI *FN)(char*, const char*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<15) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON '#'
#define DSZ 1024

static unsigned long sd = 0xC0FFEEu;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

/* The two destinations live in DIFFERENT allocations on purpose; each needs its own guard page --
   so the two return values can never be pointer-equal on success. What has to match is the
   CLASSIFICATION: NULL on both, or each returning its OWN destination. Comparing the raw pointers
   instead is a test bug, and it was one here: it passed while both sides always failed (where both
   return NULL) and fired 4095 times the moment the copy was allowed to succeed. */
static int same_ret(char* ra, char* da, char* rc, char* dc)
{
    if (ra == 0 || rc == 0) return ra == 0 && rc == 0;
    return ra == da && rc == dc;
}

/* one ordinary three-way case: the return value and the whole destination */
static int chk(const char* src, int dstoff, const char* what)
{
    static char a[DSZ], b[DSZ], c[DSZ];
    memset(a, POISON, DSZ); memset(b, POISON, DSZ); memset(c, POISON, DSZ);
    char* da = a + dstoff; char* db = b + dstoff; char* dc = c + dstoff;
    char* ra = wia_lstrcpya(da, src);
    char* rb = ref_lstrcpya(db, src);
    char* rc = sys(dc, src);
    int ok = (ra == da) && (rb == db) && (rc == dc)
          && memcmp(a, b, DSZ) == 0 && memcmp(a, c, DSZ) == 0;
    if (!ok && fails < 15)
        printf("FAIL: %s -- src len %d, dst offset %d\n", what, (int)strlen(src), dstoff);
    if (!ok) ++fails;
    return ok;
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h,"lstrcpyA");
    if (!sys) { HMODULE h2 = LoadLibraryW(L"kernel32.dll");
                sys = h2 ? (FN)GetProcAddress(h2,"lstrcpyA") : 0; }
    if(!sys){ printf("CORRECTNESS: cannot resolve lstrcpyA\n"); return 1; }
    printf("  GetACP() = %u\n", GetACP());

    static char s[2048];

    // ---- lengths 0..600 x source and destination alignments ------------------------------------
    // Both alignments matter independently: the copy clamps to the smaller of the two page
    // remainders, so a source 3 bytes from a boundary and a destination 40 bytes from one take a
    // different path than the reverse.
    {
        for (int soff = 0; soff < 40; soff += 3) {
            for (int doff = 0; doff < 40; doff += 3) {
                for (int n = 0; n <= 200; ++n) {
                    char* p = s + soff;
                    for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 23);
                    p[n] = 0;
                    chk(p, doff, "length x src alignment x dst alignment");
                }
                for (int n = 201; n <= 600; n += 13) {
                    char* p = s + soff;
                    for (int i = 0; i < n; ++i) p[i] = (char)('a' + i % 23);
                    p[n] = 0;
                    chk(p, doff, "long, x alignments");
                }
            }
        }
    }

    // ---- every byte value, at three positions ---------------------------------------------------
    {
        static char b[64];
        for (int v = 1; v < 256; ++v) {
            b[0]=(char)v; b[1]='b'; b[2]='c'; b[3]=0;   chk(b, 0, "byte value first");
            b[0]='a'; b[1]='b'; b[2]=(char)v; b[3]='d'; b[4]='e'; b[5]=0;
            chk(b, 0, "byte value middle");
            b[0]='a'; b[1]=(char)v; b[2]=0;             chk(b, 0, "byte value before the NUL");
            /* and a long run of one byte value, to drive the 64-byte path */
            for (int i = 0; i < 200; ++i) s[i] = (char)v;
            s[200] = 0;
            chk(s, 0, "200 copies of one byte value");
        }
    }

    // ---- NULLs ----------------------------------------------------------------------------------
    {
        static char a[DSZ], c[DSZ];
        memset(a, POISON, DSZ); memset(c, POISON, DSZ);
        memcpy(a, "keepme", 7); memcpy(c, "keepme", 7);
        CHECK(wia_lstrcpya(a, 0) == 0, "NULL source returns NULL");
        CHECK(sys(c, 0) == 0,          "NULL source returns NULL (live)");
        CHECK(memcmp(a, c, DSZ) == 0,  "NULL source leaves the destination byte-identical");
        CHECK(memcmp(a, "keepme", 7) == 0, "NULL source leaves the destination ALONE");
        CHECK(wia_lstrcpya(0, "abc") == 0, "NULL destination returns NULL");
        CHECK(sys(0, "abc") == 0,          "NULL destination returns NULL (live)");
        CHECK(wia_lstrcpya(0, 0) == 0,     "both NULL return NULL");
        CHECK(sys(0, 0) == 0,              "both NULL return NULL (live)");
    }

    // ---- Guard page on the source: unterminated, at every distance ------------------------------
    // Must return NULL and leave in the destination exactly what the live export leaves.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != 0, "VirtualAlloc (source guard)");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char da[8192], dc[8192];
        for (int tail = 1; tail <= 300; ++tail) {
            char* src = (base+pg) - tail;
            for (int i = 0; i < tail; ++i) src[i] = (char)('a' + i % 23);   /* NO terminator */
            for (int doff = 0; doff < 5; ++doff) {
                memset(da, POISON, sizeof da); memset(dc, POISON, sizeof dc);
                char* ra = wia_lstrcpya(da + doff, src);
                char* rc = sys(dc + doff, src);
                CHECK(same_ret(ra, da + doff, rc, dc + doff),
                      "source guard: same return classification");
                CHECK(memcmp(da, dc, sizeof da) == 0,
                      "source guard: the PARTIAL COPY must be byte-identical to the live export");
                if (memcmp(da, dc, sizeof da) != 0 && fails < 16) {
                    int i = 0; while (i < 400 && da[i] == dc[i]) ++i;
                    printf("   (tail %d, dst offset %d: first difference at byte %d, ours 0x%02X "
                           "live 0x%02X)\n", tail, doff, i,
                           (unsigned char)da[i], (unsigned char)dc[i]);
                }
            }
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    // ---- Guard page on the destination: too small, at every distance ----------------------------
    // THIS is the section that catches a source-only page clamp. The live export fills the
    // destination to its last writable byte and returns NULL; an implementation that writes a whole
    // 32-byte chunk gets a different amount in and is still "correct" by every other measure.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* ba = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        char* bc = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(ba != 0 && bc != 0, "VirtualAlloc (destination guards)");
        DWORD old;
        VirtualProtect(ba+pg, pg, PAGE_NOACCESS, &old);
        VirtualProtect(bc+pg, pg, PAGE_NOACCESS, &old);
        for (int srclen = 40; srclen <= 400; srclen += 37) {
            for (int i = 0; i < srclen; ++i) s[i] = (char)('a' + i % 23);
            s[srclen] = 0;
            for (int room = 1; room <= 300; ++room) {
                if (room > srclen) break;          /* it would fit: not the case under test */
                char* da = (ba+pg) - room;
                char* dc = (bc+pg) - room;
                memset(da, POISON, room);
                memset(dc, POISON, room);
                char* ra = wia_lstrcpya(da, s);
                char* rc = sys(dc, s);
                CHECK(same_ret(ra, da, rc, dc),
                      "destination guard: same return classification");
                CHECK(memcmp(da, dc, room) == 0,
                      "destination guard: filled to the SAME byte as the live export");
                if (memcmp(da, dc, room) != 0 && fails < 16) {
                    int i = 0; while (i < room && da[i] == dc[i]) ++i;
                    printf("   (srclen %d, room %d: first difference at byte %d, ours 0x%02X "
                           "live 0x%02X)\n", srclen, room, i,
                           (unsigned char)da[i], (unsigned char)dc[i]);
                }
            }
        }
        VirtualFree(ba, 0, MEM_RELEASE);
        VirtualFree(bc, 0, MEM_RELEASE);
    }

    // ---- both guarded at once: the clamp has to take the smaller remainder -----------------------
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* bs = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        char* ba = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        char* bc = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(bs && ba && bc, "VirtualAlloc (both guarded)");
        DWORD old;
        VirtualProtect(bs+pg, pg, PAGE_NOACCESS, &old);
        VirtualProtect(ba+pg, pg, PAGE_NOACCESS, &old);
        VirtualProtect(bc+pg, pg, PAGE_NOACCESS, &old);
        for (int stail = 1; stail <= 90; ++stail) {
            char* src = (bs+pg) - stail;
            for (int i = 0; i < stail-1; ++i) src[i] = (char)('a' + i % 23);
            src[stail-1] = 0;                       /* terminated exactly at the edge */
            for (int room = 1; room <= 90; ++room) {
                char* da = (ba+pg) - room;
                char* dc = (bc+pg) - room;
                memset(da, POISON, room);
                memset(dc, POISON, room);
                char* ra = wia_lstrcpya(da, src);
                char* rc = sys(dc, src);
                CHECK(same_ret(ra, da, rc, dc),
                      "both guarded: same return classification");
                CHECK(memcmp(da, dc, room) == 0, "both guarded: same bytes transferred");
            }
        }
        VirtualFree(bs, 0, MEM_RELEASE);
        VirtualFree(ba, 0, MEM_RELEASE);
        VirtualFree(bc, 0, MEM_RELEASE);
    }

    // ---- randomized fuzz -------------------------------------------------------------------------
    {
        for (int t = 0; t < 200000; ++t) {
            int n = rnd() % 500;
            int soff = rnd() % 40, doff = rnd() % 40;
            char* p = s + soff;
            for (int i = 0; i < n; ++i) { int v = 1 + (int)(rnd() % 255); p[i] = (char)v; }
            p[n] = 0;
            chk(p, doff, "fuzz");
        }
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (lstrcpyA vs live kernelbase + oracle, WHOLE-BUFFER compare against "
           "poison: lengths 0..600 x 14 source alignments x 14 destination alignments, ALL 255 "
           "non-NUL byte values at three positions and as a 200-byte run, all NULL combinations "
           "including that a NULL source leaves the destination ALONE, 200k fuzz, and THREE "
           "guard-page sweeps -- an unterminated SOURCE at every distance 1..300, a DESTINATION "
           "too small at every room 1..300 (the sweep that catches a source-only page clamp, "
           "because the live export fills to its last writable byte and returns NULL), and BOTH "
           "guarded at once so the clamp has to take the smaller remainder)\n");
    return 0;
}
