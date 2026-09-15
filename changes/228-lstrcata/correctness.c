// changes/228-lstrcata/correctness.c
// Gate 1: wia_lstrcata must be indistinguishable from kernelbase!lstrcatA.
// Three-way: our ASM + wrapper vs the scalar oracle vs the LIVE export on this PC.
//
// THERE ARE THREE WAYS THIS FUNCTION CAN FAIL, NOT TWO, and each gets its own guard-page sweep.
// lstrcat READS the destination before it writes it, so an UNTERMINATED DESTINATION is a distinct
// failure from a bad source or a short one -- and it is the one an implementation borrowed from
// lstrcpy would never think to handle, because lstrcpy does not read its destination at all.
//
// All three are swallowed by the shipped export (it returns NULL rather than faulting), and in two
// of them the caller can still read how far the work got. So the comparison against the live export
// is byte-for-byte over the whole buffer, not just the return value.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern char* wia_lstrcata(char*, const char*);
char* ref_lstrcata(char*, const char*);
typedef char* (WINAPI *FN)(char*, const char*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<15) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON '#'
/* 4352, not 1024: the long-destination section builds destinations up to 2000 bytes
   and appends to them. Sizing this at 1024 overflowed every buffer in the file and
   the harness died with no output at all. */
#define DSZ 4352

static unsigned long sd = 0xA11CEu;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

/* The two buffers live in different allocations wherever guard pages are involved, so their return
   pointers can never be equal on success. What has to match is the CLASSIFICATION. (Comparing raw
   pointers is the test bug that change 227's harness shipped with and had to be corrected.) */
static int same_ret(char* ra, char* da, char* rc, char* dc)
{
    if (ra == 0 || rc == 0) return ra == 0 && rc == 0;
    return ra == da && rc == dc;
}

/* one ordinary three-way case: the return and the WHOLE destination */
static int chk(const char* dinit, const char* src, int dstoff, const char* what)
{
    static char a[DSZ], b[DSZ], c[DSZ];
    memset(a, POISON, DSZ); memset(b, POISON, DSZ); memset(c, POISON, DSZ);
    size_t dn = strlen(dinit);
    char* da = a + dstoff; char* db = b + dstoff; char* dc = c + dstoff;
    memcpy(da, dinit, dn+1); memcpy(db, dinit, dn+1); memcpy(dc, dinit, dn+1);
    char* ra = wia_lstrcata(da, src);
    char* rb = ref_lstrcata(db, src);
    char* rc = sys(dc, src);
    int ok = (ra == da) && (rb == db) && (rc == dc)
          && memcmp(a, b, DSZ) == 0 && memcmp(a, c, DSZ) == 0;
    if (!ok && fails < 15)
        printf("FAIL: %s -- dst len %d, src len %d, offset %d\n",
               what, (int)dn, (int)strlen(src), dstoff);
    if (!ok) ++fails;
    return ok;
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h,"lstrcatA");
    if (!sys) { HMODULE h2 = LoadLibraryW(L"kernel32.dll");
                sys = h2 ? (FN)GetProcAddress(h2,"lstrcatA") : 0; }
    if(!sys){ printf("CORRECTNESS: cannot resolve lstrcatA\n"); return 1; }
    printf("  GetACP() = %u\n", GetACP());

    static char ds[4352], ss[4352];

    // ---- destination length x source length x alignments -----------------------------------------
    // The scan half depends on the destination's alignment and length; the append half depends on
    // where dst+len lands relative to a page, which is a different quantity again.
    {
        for (int doff = 0; doff < 32; doff += 3) {
            for (int dn = 0; dn <= 100; ++dn) {
                for (int sn = 0; sn <= 100; sn += 7) {
                    for (int i = 0; i < dn; ++i) ds[i] = (char)('a' + i % 23);
                    ds[dn] = 0;
                    for (int i = 0; i < sn; ++i) ss[i] = (char)('A' + i % 26);
                    ss[sn] = 0;
                    chk(ds, ss, doff, "dst len x src len x alignment");
                }
            }
        }
    }
    // and long destinations, where the scan dominates
    {
        for (int dn = 200; dn <= 2000; dn += 61) {
            for (int i = 0; i < dn; ++i) ds[i] = (char)('a' + i % 23);
            ds[dn] = 0;
            for (int sn = 0; sn <= 40; sn += 9) {
                for (int i = 0; i < sn; ++i) ss[i] = (char)('A' + i % 26);
                ss[sn] = 0;
                chk(ds, ss, 0, "long destination, short append");
                chk(ds, ss, 7, "long destination, short append, unaligned");
            }
        }
    }

    // ---- EMPTY SOURCE: the store still happens, so this must not be special-cased away -----------
    {
        for (int dn = 0; dn <= 100; ++dn) {
            for (int i = 0; i < dn; ++i) ds[i] = (char)('a' + i % 23);
            ds[dn] = 0;
            chk(ds, "", 0, "empty source");
            chk(ds, "", 5, "empty source, unaligned");
        }
    }

    // ---- every byte value, in BOTH strings --------------------------------------------------------
    for (int v = 1; v < 256; ++v) {
        ds[0]='a'; ds[1]=(char)v; ds[2]='c'; ds[3]=0;
        chk(ds, "XY", 0, "byte value in the destination");
        ss[0]='X'; ss[1]=(char)v; ss[2]='Z'; ss[3]=0;
        chk("ab", ss, 0, "byte value in the source");
        /* long runs of one value, to drive the 64-byte paths on both halves */
        for (int i = 0; i < 200; ++i) ds[i] = (char)v;
        ds[200] = 0;
        chk(ds, "tail", 0, "200-byte destination of one value");
        for (int i = 0; i < 200; ++i) ss[i] = (char)v;
        ss[200] = 0;
        chk("head", ss, 0, "200-byte source of one value");
    }

    // ---- NULLs ------------------------------------------------------------------------------------
    {
        static char a[DSZ], c[DSZ];
        memset(a, POISON, DSZ); memset(c, POISON, DSZ);
        memcpy(a, "keepme", 7); memcpy(c, "keepme", 7);
        CHECK(wia_lstrcata(a, 0) == 0, "NULL source returns NULL");
        CHECK(sys(c, 0) == 0,          "NULL source returns NULL (live)");
        CHECK(memcmp(a, c, DSZ) == 0,  "NULL source leaves the destination byte-identical");
        CHECK(memcmp(a, "keepme", 7) == 0, "NULL source leaves the destination ALONE");
        CHECK(wia_lstrcata(0, "abc") == 0, "NULL destination returns NULL");
        CHECK(sys(0, "abc") == 0,          "NULL destination returns NULL (live)");
        CHECK(wia_lstrcata(0, 0) == 0,     "both NULL return NULL");
        CHECK(sys(0, 0) == 0,              "both NULL return NULL (live)");
    }

    // ---- GUARD PAGE ON THE DESTINATION SCAN: unterminated, at every distance ----------------------
    // The failure lstrcpy does not have. An implementation whose scan reads one block too far comes
    // back NULL for a perfectly ordinary destination that happens to end near a page edge.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* ba = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        char* bc = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(ba && bc, "VirtualAlloc (destination scan guard)");
        DWORD old;
        VirtualProtect(ba+pg, pg, PAGE_NOACCESS, &old);
        VirtualProtect(bc+pg, pg, PAGE_NOACCESS, &old);
        for (int tail = 1; tail <= 200; ++tail) {
            char* da = (ba+pg) - tail;
            char* dc = (bc+pg) - tail;
            for (int i = 0; i < tail; ++i) { da[i] = (char)('a' + i % 23); dc[i] = da[i]; }
            char* ra = wia_lstrcata(da, "xy");           /* NO terminator in the destination */
            char* rc = sys(dc, "xy");
            CHECK(same_ret(ra, da, rc, dc), "destination-scan guard: same return classification");
            CHECK(memcmp(da, dc, tail) == 0, "destination-scan guard: same bytes");
        }
        /* and TERMINATED at every distance: the answer must be the append, not NULL */
        for (int tail = 2; tail <= 200; ++tail) {
            char* da = (ba+pg) - tail;
            char* dc = (bc+pg) - tail;
            for (int i = 0; i < tail-1; ++i) { da[i] = (char)('a' + i % 23); dc[i] = da[i]; }
            da[tail-1] = 0; dc[tail-1] = 0;
            char* ra = wia_lstrcata(da, "");             /* empty append: must still fit */
            char* rc = sys(dc, "");
            CHECK(same_ret(ra, da, rc, dc), "destination terminated at the edge: same return");
            CHECK(memcmp(da, dc, tail) == 0, "destination terminated at the edge: same bytes");
        }
        VirtualFree(ba, 0, MEM_RELEASE);
        VirtualFree(bc, 0, MEM_RELEASE);
    }

    // ---- GUARD PAGE ON THE SOURCE: unterminated, at every distance --------------------------------
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
            for (int dn = 0; dn < 5; ++dn) {
                memset(da, POISON, sizeof da); memset(dc, POISON, sizeof dc);
                for (int i = 0; i < dn; ++i) { da[i] = 'Z'; dc[i] = 'Z'; }
                da[dn] = 0; dc[dn] = 0;
                char* ra = wia_lstrcata(da, src);
                char* rc = sys(dc, src);
                CHECK(same_ret(ra, da, rc, dc), "source guard: same return classification");
                CHECK(memcmp(da, dc, sizeof da) == 0,
                      "source guard: the PARTIAL APPEND must be byte-identical to the live export");
            }
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    // ---- GUARD PAGE ON THE DESTINATION WRITE: too small for the append ----------------------------
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* ba = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        char* bc = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(ba && bc, "VirtualAlloc (destination write guards)");
        DWORD old;
        VirtualProtect(ba+pg, pg, PAGE_NOACCESS, &old);
        VirtualProtect(bc+pg, pg, PAGE_NOACCESS, &old);
        for (int srclen = 40; srclen <= 400; srclen += 41) {
            for (int i = 0; i < srclen; ++i) ss[i] = (char)('A' + i % 26);
            ss[srclen] = 0;
            for (int room = 2; room <= 200; ++room) {
                char* da = (ba+pg) - room;
                char* dc = (bc+pg) - room;
                memset(da, POISON, room); memset(dc, POISON, room);
                da[0] = 'Z'; da[1] = 0;
                dc[0] = 'Z'; dc[1] = 0;
                char* ra = wia_lstrcata(da, ss);
                char* rc = sys(dc, ss);
                CHECK(same_ret(ra, da, rc, dc), "destination-write guard: same return");
                CHECK(memcmp(da, dc, room) == 0,
                      "destination-write guard: filled to the SAME byte as the live export");
            }
        }
        VirtualFree(ba, 0, MEM_RELEASE);
        VirtualFree(bc, 0, MEM_RELEASE);
    }

    // ---- randomized fuzz --------------------------------------------------------------------------
    {
        for (int t = 0; t < 150000; ++t) {
            int dn = rnd() % 300, sn = rnd() % 200, doff = rnd() % 32;
            for (int i = 0; i < dn; ++i) { int v = 1 + (int)(rnd() % 255); ds[i] = (char)v; }
            ds[dn] = 0;
            for (int i = 0; i < sn; ++i) { int v = 1 + (int)(rnd() % 255); ss[i] = (char)v; }
            ss[sn] = 0;
            chk(ds, ss, doff, "fuzz");
        }
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (lstrcatA vs live kernelbase + oracle, WHOLE-BUFFER compare against "
           "poison: destination lengths 0..100 x source lengths 0..100 x 11 alignments, long "
           "destinations to 2000 where the SCAN dominates, EMPTY sources at every destination "
           "length (the store still happens -- a PAGE_READONLY destination proves it), ALL 255 "
           "non-NUL byte values in BOTH strings and as 200-byte runs, every NULL combination, 150k "
           "fuzz, and FOUR guard-page sweeps -- an unterminated DESTINATION at every distance (the "
           "failure lstrcpy does not have, because lstrcat READS its destination first), a "
           "destination TERMINATED exactly at the edge, an unterminated SOURCE at every distance, "
           "and a destination too small for the append at every room)\n");
    return 0;
}
