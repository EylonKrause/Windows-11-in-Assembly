// changes/230-lstrcatw/correctness.c
// Gate 1: wia_lstrcatw must be indistinguishable from kernelbase!lstrcatW.
// Three-way: our ASM + wrapper vs the scalar oracle vs the LIVE export on this PC.
//
// Four guard-page sweeps, because this function can fail on three pointers and at two granularities:
//
//   * an unterminated destination -- the failure lstrcpy does not have at all, since lstrcat reads
//     the destination before writing it. An implementation borrowed from lstrcpy would never think
//     to bound that scan.
//   * an unterminated SOURCE.
//   * a destination too small for the append.
//   * The split character: every destination width in bytes, odd and even. The export writes whole
//     CHARACTERS only, so a clamp that rounds in bytes leaves one extra byte behind -- invisible to
//     every other test here, because nothing crashes and no return value differs.
//
// Odd-aligned destinations are driven throughout, not only at the guard. The destination scan uses
// the page clamp rather than an align-down trick precisely because this function accepts them, and
// an align-down scan would put its 16-bit lanes out of step with the string's characters.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern wchar_t* wia_lstrcatw(wchar_t*, const wchar_t*);
wchar_t* ref_lstrcatw(wchar_t*, const wchar_t*);
typedef wchar_t* (WINAPI *FN)(wchar_t*, const wchar_t*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<15) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON 0x2A2A
#define DSZ 4352

static unsigned long sd = 0x13579u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

static int same_ret(wchar_t* ra, wchar_t* da, wchar_t* rc, wchar_t* dc)
{
    if (ra == 0 || rc == 0) return ra == 0 && rc == 0;
    return ra == da && rc == dc;
}

/* one ordinary three-way case. dstoff is in CHARACTERS; oddbyte shifts the destination by one BYTE
   so odd-aligned destinations are on the ordinary path too. */
static int chk(const wchar_t* dinit, const wchar_t* src, int dstoff, int oddbyte, const char* what)
{
    static char a[DSZ*2], b[DSZ*2], c[DSZ*2];
    memset(a, 0x2A, sizeof a); memset(b, 0x2A, sizeof b); memset(c, 0x2A, sizeof c);
    size_t dn = wcslen(dinit);
    wchar_t* da = (wchar_t*)(a + dstoff*2 + oddbyte);
    wchar_t* db = (wchar_t*)(b + dstoff*2 + oddbyte);
    wchar_t* dc = (wchar_t*)(c + dstoff*2 + oddbyte);
    memcpy(da, dinit, (dn+1)*sizeof(wchar_t));
    memcpy(db, dinit, (dn+1)*sizeof(wchar_t));
    memcpy(dc, dinit, (dn+1)*sizeof(wchar_t));
    wchar_t* ra = wia_lstrcatw(da, src);
    wchar_t* rb = ref_lstrcatw(db, src);
    wchar_t* rc = sys(dc, src);
    int ok = (ra == da) && (rb == db) && (rc == dc)
          && memcmp(a, b, sizeof a) == 0 && memcmp(a, c, sizeof a) == 0;
    if (!ok && fails < 15)
        printf("FAIL: %s -- dst len %d, src len %d, offset %d, oddbyte %d\n",
               what, (int)dn, (int)wcslen(src), dstoff, oddbyte);
    if (!ok) ++fails;
    return ok;
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h,"lstrcatW");
    if (!sys) { HMODULE h2 = LoadLibraryW(L"kernel32.dll");
                sys = h2 ? (FN)GetProcAddress(h2,"lstrcatW") : 0; }
    if(!sys){ printf("CORRECTNESS: cannot resolve lstrcatW\n"); return 1; }

    static wchar_t ds[2200], ss[2200];

    // ---- destination length x source length x alignment x PARITY ---------------------------------
    for (int oddbyte = 0; oddbyte < 2; ++oddbyte) {
        for (int doff = 0; doff < 18; doff += 3) {
            for (int dn = 0; dn <= 80; ++dn) {
                for (int sn = 0; sn <= 80; sn += 7) {
                    for (int i = 0; i < dn; ++i) ds[i] = (wchar_t)(L'a' + i % 23);
                    ds[dn] = 0;
                    for (int i = 0; i < sn; ++i) ss[i] = (wchar_t)(L'A' + i % 26);
                    ss[sn] = 0;
                    chk(ds, ss, doff, oddbyte, "dst len x src len x alignment x parity");
                }
            }
        }
    }
    // long destinations, where the scan dominates -- and the scan is the half that had to be
    // written with a page clamp instead of an align-down, for the odd-aligned case
    for (int oddbyte = 0; oddbyte < 2; ++oddbyte) {
        for (int dn = 200; dn <= 1800; dn += 57) {
            for (int i = 0; i < dn; ++i) ds[i] = (wchar_t)(L'a' + i % 23);
            ds[dn] = 0;
            for (int sn = 0; sn <= 40; sn += 9) {
                for (int i = 0; i < sn; ++i) ss[i] = (wchar_t)(L'A' + i % 26);
                ss[sn] = 0;
                chk(ds, ss, 0, oddbyte, "long destination, short append");
            }
        }
    }

    // ---- EMPTY SOURCE: the store still happens ----------------------------------------------------
    for (int oddbyte = 0; oddbyte < 2; ++oddbyte)
        for (int dn = 0; dn <= 80; ++dn) {
            for (int i = 0; i < dn; ++i) ds[i] = (wchar_t)(L'a' + i % 23);
            ds[dn] = 0;
            chk(ds, L"", 3, oddbyte, "empty source");
        }

    // ---- every code unit value, in both strings ---------------------------------------------------
    {
        static wchar_t b[64];
        for (int v = 1; v < 65536; ++v) {
            b[0]=L'a'; b[1]=(wchar_t)v; b[2]=L'c'; b[3]=0;
            chk(b, L"XY", 0, v & 1, "code unit in the destination");
            b[0]=L'X'; b[1]=(wchar_t)v; b[2]=L'Z'; b[3]=0;
            chk(L"ab", b, 0, v & 1, "code unit in the source");
        }
        for (int v = 1; v < 65536; v += 97) {
            for (int i = 0; i < 200; ++i) ds[i] = (wchar_t)v;
            ds[200] = 0;
            chk(ds, L"tail", 0, 0, "200-character destination of one value");
        }
    }

    // ---- NULLs ------------------------------------------------------------------------------------
    {
        static wchar_t a[DSZ], c[DSZ];
        for (int i = 0; i < DSZ; ++i) { a[i] = POISON; c[i] = POISON; }
        memcpy(a, L"keepme", 7*sizeof(wchar_t));
        memcpy(c, L"keepme", 7*sizeof(wchar_t));
        CHECK(wia_lstrcatw(a, 0) == 0, "NULL source returns NULL");
        CHECK(sys(c, 0) == 0,          "NULL source returns NULL (live)");
        CHECK(memcmp(a, c, sizeof a) == 0, "NULL source leaves the destination byte-identical");
        CHECK(wcscmp(a, L"keepme") == 0,   "NULL source leaves the destination ALONE");
        CHECK(wia_lstrcatw(0, L"abc") == 0, "NULL destination returns NULL");
        CHECK(sys(0, L"abc") == 0,          "NULL destination returns NULL (live)");
        CHECK(wia_lstrcatw(0, 0) == 0,      "both NULL return NULL");
        CHECK(sys(0, 0) == 0,               "both NULL return NULL (live)");
    }

    // ---- Guard page on the destination scan -------------------------------------------------------
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* ba = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        char* bc = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(ba && bc, "VirtualAlloc (destination scan guards)");
        DWORD old;
        VirtualProtect(ba+pg, pg, PAGE_NOACCESS, &old);
        VirtualProtect(bc+pg, pg, PAGE_NOACCESS, &old);
        /* UNTERMINATED, at every width in BYTES so both parities are covered */
        for (int wbytes = 2; wbytes <= 200; ++wbytes) {
            char* da = (ba+pg) - wbytes;
            char* dc = (bc+pg) - wbytes;
            memset(da, 0x41, wbytes); memset(dc, 0x41, wbytes);
            wchar_t* ra = wia_lstrcatw((wchar_t*)da, L"xy");
            wchar_t* rc = sys((wchar_t*)dc, L"xy");
            CHECK(same_ret(ra, (wchar_t*)da, rc, (wchar_t*)dc),
                  "destination-scan guard: same return");
            CHECK(memcmp(da, dc, wbytes) == 0, "destination-scan guard: same bytes");
        }
        /* TERMINATED exactly at the edge: the answer must be the append, not NULL */
        for (int wchars = 2; wchars <= 100; ++wchars) {
            wchar_t* da = (wchar_t*)(ba+pg) - wchars;
            wchar_t* dc = (wchar_t*)(bc+pg) - wchars;
            for (int i = 0; i < wchars-1; ++i) { da[i] = L'a'; dc[i] = L'a'; }
            da[wchars-1] = 0; dc[wchars-1] = 0;
            wchar_t* ra = wia_lstrcatw(da, L"");
            wchar_t* rc = sys(dc, L"");
            CHECK(same_ret(ra, da, rc, dc), "destination terminated at the edge: same return");
            CHECK(memcmp(da, dc, (size_t)wchars*sizeof(wchar_t)) == 0,
                  "destination terminated at the edge: same bytes");
        }
        VirtualFree(ba, 0, MEM_RELEASE);
        VirtualFree(bc, 0, MEM_RELEASE);
    }

    // ---- Guard page on the source -----------------------------------------------------------------
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != 0, "VirtualAlloc (source guard)");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static wchar_t da[4096], dc[4096];
        for (int tail = 1; tail <= 200; ++tail) {
            wchar_t* src = (wchar_t*)(base+pg) - tail;
            for (int i = 0; i < tail; ++i) src[i] = (wchar_t)(L'a' + i % 23);
            for (int dn = 0; dn < 4; ++dn) {
                for (int i = 0; i < 4096; ++i) { da[i] = POISON; dc[i] = POISON; }
                for (int i = 0; i < dn; ++i) { da[i] = L'Z'; dc[i] = L'Z'; }
                da[dn] = 0; dc[dn] = 0;
                wchar_t* ra = wia_lstrcatw(da, src);
                wchar_t* rc = sys(dc, src);
                CHECK(same_ret(ra, da, rc, dc), "source guard: same return");
                CHECK(memcmp(da, dc, sizeof da) == 0,
                      "source guard: the PARTIAL APPEND must be byte-identical to the live export");
            }
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    // ---- The split character: every destination width in bytes, odd and even ----------------------
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* ba = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        char* bc = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(ba && bc, "VirtualAlloc (split-character guards)");
        DWORD old;
        VirtualProtect(ba+pg, pg, PAGE_NOACCESS, &old);
        VirtualProtect(bc+pg, pg, PAGE_NOACCESS, &old);
        for (int srclen = 8; srclen <= 200; srclen += 23) {
            for (int i = 0; i < srclen; ++i) ss[i] = (wchar_t)(L'A' + i % 26);
            ss[srclen] = 0;
            for (int wbytes = 4; wbytes <= 201; ++wbytes) {
                char* da = (ba+pg) - wbytes;
                char* dc = (bc+pg) - wbytes;
                memset(da, 0x5A, wbytes); memset(dc, 0x5A, wbytes);
                ((wchar_t*)da)[0] = L'Z'; ((wchar_t*)da)[1] = 0;
                ((wchar_t*)dc)[0] = L'Z'; ((wchar_t*)dc)[1] = 0;
                wchar_t* ra = wia_lstrcatw((wchar_t*)da, ss);
                wchar_t* rc = sys((wchar_t*)dc, ss);
                CHECK(same_ret(ra, (wchar_t*)da, rc, (wchar_t*)dc), "split character: same return");
                CHECK(memcmp(da, dc, wbytes) == 0,
                      "split character: the SAME bytes changed as the live export");
                if (memcmp(da, dc, wbytes) != 0 && fails < 16) {
                    int i = 0; while (i < wbytes && da[i] == dc[i]) ++i;
                    printf("   (srclen %d, %d writable byte(s): first difference at byte %d, "
                           "ours 0x%02X live 0x%02X)\n", srclen, wbytes, i,
                           (unsigned char)da[i], (unsigned char)dc[i]);
                }
            }
        }
        VirtualFree(ba, 0, MEM_RELEASE);
        VirtualFree(bc, 0, MEM_RELEASE);
    }

    // ---- randomized fuzz --------------------------------------------------------------------------
    {
        for (int t = 0; t < 120000; ++t) {
            int dn = rnd() % 250, sn = rnd() % 150, doff = rnd() % 18, ob = rnd() & 1;
            for (int i = 0; i < dn; ++i) ds[i] = (wchar_t)(1 + (rnd() % 65535));
            ds[dn] = 0;
            for (int i = 0; i < sn; ++i) ss[i] = (wchar_t)(1 + (rnd() % 65535));
            ss[sn] = 0;
            chk(ds, ss, doff, ob, "fuzz");
        }
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (lstrcatW vs live kernelbase + oracle, WHOLE-BUFFER compare against "
           "poison, with ODD-ALIGNED destinations driven throughout: destination lengths 0..80 x "
           "source lengths 0..80 x 6 alignments x BOTH parities, long destinations to 1800 where "
           "the scan dominates, empty sources, ALL 65535 code unit values in BOTH strings, every "
           "NULL combination, 120k fuzz, and FOUR guard-page sweeps -- an unterminated DESTINATION "
           "at every width in BYTES (the failure lstrcpy does not have, because lstrcat READS its "
           "destination first), a destination TERMINATED exactly at the edge, an unterminated "
           "SOURCE at every distance, and THE SPLIT CHARACTER at every destination width 4..201 "
           "bytes because the export writes whole characters only)\n");
    return 0;
}
