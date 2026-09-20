// changes/231-strcatbuffa/correctness.c
// Gate 1: wia_strcatbuffa must be indistinguishable from shlwapi!StrCatBuffA.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
//
// Two things here are not ordinary, and both come straight from the probes.
//
//   * The whole buffer is compared against a poison fill, and the function's most distinctive rule
//     is invisible any other way: when no terminator is found within the first cch bytes it writes
//     Nothing at all; it does not truncate the destination and it does not append. Only poison can
//     tell "wrote nothing" from "wrote a terminator where one already was".
//   * The fault case is a fault, not a return. Unlike lstrcpy and lstrcat, StrCatBuffA does not
//     swallow an access violation: when the caller lies about cch it faults, 37 of 37 distances
//     (probes/scb2.c). So the harness catches the exception itself and compares WHETHER each side
//     faulted and HOW MUCH each had written first; there is no NULL return to compare instead.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern char* wia_strcatbuffa(char*, const char*, int);
char* ref_strcatbuffa(char*, const char*, int);
typedef char* (WINAPI *FN)(char*, const char*, int);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<15) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON '#'
/* 1200, not 512: the long section builds a 396-character destination and appends 119 more,
   which overflows a 512-byte harness buffer. It showed up as a single mysterious FAIL at
   exactly that length rather than as a crash. */
#define DSZ 1200

static unsigned long sd = 0x9E3779u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

/* one ordinary three-way case: the return value and the whole buffer */
static int chk(const char* dinit, const char* src, int cch, int dstoff, const char* what)
{
    static char a[DSZ], b[DSZ], c[DSZ];
    memset(a, POISON, DSZ); memset(b, POISON, DSZ); memset(c, POISON, DSZ);
    size_t dn = strlen(dinit);
    char* da = a + dstoff; char* db = b + dstoff; char* dc = c + dstoff;
    memcpy(da, dinit, dn+1); memcpy(db, dinit, dn+1); memcpy(dc, dinit, dn+1);
    char* ra = wia_strcatbuffa(da, src, cch);
    char* rb = ref_strcatbuffa(db, src, cch);
    char* rc = sys(dc, src, cch);
    int ok = (ra == da) && (rb == db) && (rc == dc)
          && memcmp(a, b, DSZ) == 0 && memcmp(a, c, DSZ) == 0;
    if (!ok && fails < 15)
        printf("FAIL: %s -- dst len %d, src len %d, cch %d, offset %d\n",
               what, (int)dn, (int)strlen(src), cch, dstoff);
    if (!ok) ++fails;
    return ok;
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(h,"StrCatBuffA");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!StrCatBuffA\n"); return 1; }
    printf("  GetACP() = %u\n", GetACP());

    static char ds[600], ss[600];

    // ---- EXHAUSTIVE over the three dimensions that interact -------------------------------------
    // destination length x source length x cch. The bound is the whole point of this function, and
    // the interesting behaviour is entirely at the boundaries between "fits", "truncates" and
    // "the destination already exceeds the bound".
    {
        long n = 0;
        for (int dn = 0; dn <= 40; ++dn) {
            for (int sn = 0; sn <= 40; ++sn) {
                for (int cch = 0; cch <= 90; ++cch) {
                    for (int i = 0; i < dn; ++i) ds[i] = (char)('a' + i % 23);
                    ds[dn] = 0;
                    for (int i = 0; i < sn; ++i) ss[i] = (char)('A' + i % 26);
                    ss[sn] = 0;
                    chk(ds, ss, cch, 0, "exhaustive dn x sn x cch");
                    ++n;
                }
            }
        }
        printf("  exhaustive dn 0..40 x sn 0..40 x cch 0..90: %ld cases\n", n);
    }

    // ---- NEGATIVE and extreme cch ----------------------------------------------------------------
    {
        static const int CCH[] = { -1, -2, -1000, 0, 1, 2, 0x7FFFFFFF, 0 };
        for (int i = 0; CCH[i] || i < 7; ++i) {
            chk("abc", "XY", CCH[i], 0, "extreme cch");
            chk("", "XY", CCH[i], 0, "extreme cch, empty destination");
            if (i >= 7) break;
        }
    }

    // ---- alignments ------------------------------------------------------------------------------
    for (int off = 0; off < 40; ++off) {
        for (int dn = 0; dn <= 40; dn += 3) {
            for (int sn = 0; sn <= 40; sn += 5) {
                for (int i = 0; i < dn; ++i) ds[i] = (char)('a' + i % 23);
                ds[dn] = 0;
                for (int i = 0; i < sn; ++i) ss[i] = (char)('A' + i % 26);
                ss[sn] = 0;
                chk(ds, ss, dn + sn + 1, off, "alignment, exact fit");
                chk(ds, ss, dn + sn,     off, "alignment, one short");
                chk(ds, ss, 200,         off, "alignment, room to spare");
            }
        }
    }

    // ---- long strings, which drive the 32-byte paths ---------------------------------------------
    for (int dn = 100; dn <= 400; dn += 37) {
        for (int i = 0; i < dn; ++i) ds[i] = (char)('a' + i % 23);
        ds[dn] = 0;
        for (int sn = 0; sn <= 120; sn += 17) {
            for (int i = 0; i < sn; ++i) ss[i] = (char)('A' + i % 26);
            ss[sn] = 0;
            chk(ds, ss, dn + sn + 1, 0, "long, exact fit");
            chk(ds, ss, dn + sn / 2, 0, "long, truncating");
            chk(ds, ss, dn,          0, "long, destination fills the bound");
            chk(ds, ss, dn - 10,     0, "long, destination EXCEEDS the bound");
            chk(ds, ss, 500,         0, "long, room to spare");
        }
    }

    // ---- every byte value, in both strings -------------------------------------------------------
    for (int v = 1; v < 256; ++v) {
        ds[0]='a'; ds[1]=(char)v; ds[2]='c'; ds[3]=0;
        chk(ds, "XY", 20, 0, "byte value in the destination");
        ss[0]='X'; ss[1]=(char)v; ss[2]='Z'; ss[3]=0;
        chk("ab", ss, 20, 0, "byte value in the source");
        for (int i = 0; i < 100; ++i) ds[i] = (char)v;
        ds[100] = 0;
        chk(ds, "tail", 200, 0, "100-byte destination of one value");
    }

    // ---- NULLs ------------------------------------------------------------------------------------
    {
        static char a[DSZ], c[DSZ];
        memset(a, POISON, DSZ); memset(c, POISON, DSZ);
        memcpy(a, "keep", 5); memcpy(c, "keep", 5);
        CHECK(wia_strcatbuffa(a, 0, 40) == a, "NULL source returns the DESTINATION");
        CHECK(sys(c, 0, 40) == c,             "NULL source returns the DESTINATION (live)");
        CHECK(memcmp(a, c, DSZ) == 0,         "NULL source: buffers byte-identical");
        CHECK(memcmp(a, "keep", 5) == 0,      "NULL source leaves the destination ALONE");
        CHECK(wia_strcatbuffa(0, "abc", 40) == 0, "NULL destination returns NULL");
        CHECK(sys(0, "abc", 40) == 0,             "NULL destination returns NULL (live)");
        CHECK(wia_strcatbuffa(0, 0, 40) == 0,     "both NULL return NULL");
        CHECK(sys(0, 0, 40) == 0,                 "both NULL return NULL (live)");
    }

    // ---- The bound holds: an unterminated destination at a guard page, cch == the room ------------
    // The scan must stop at cch and write nothing. If it ran past the bound it would touch the
    // guard; if it wrote a terminator it would differ from the live export byte for byte.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* ba = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        char* bc = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(ba && bc, "VirtualAlloc (bound guards)");
        DWORD old;
        VirtualProtect(ba+pg, pg, PAGE_NOACCESS, &old);
        VirtualProtect(bc+pg, pg, PAGE_NOACCESS, &old);
        for (int room = 1; room <= 200; ++room) {
            /* (a) no terminator anywhere inside cch */
            char* da = (ba+pg) - room;
            char* dc = (bc+pg) - room;
            memset(da, 'a', room); memset(dc, 'a', room);
            char* ra = wia_strcatbuffa(da, "Z", room);
            char* rc = sys(dc, "Z", room);
            CHECK(ra == da && rc == dc, "bounded scan: both return the destination");
            CHECK(memcmp(da, dc, room) == 0, "bounded scan: both wrote NOTHING, identically");

            /* (b) terminated exactly at the last byte the bound allows */
            memset(da, 'a', room); memset(dc, 'a', room);
            da[room-1] = 0; dc[room-1] = 0;
            ra = wia_strcatbuffa(da, "Z", room);
            rc = sys(dc, "Z", room);
            CHECK(ra == da && rc == dc, "terminator at cch-1: both return the destination");
            CHECK(memcmp(da, dc, room) == 0, "terminator at cch-1: identical buffers");

            /* (c) a short string with the bound exactly at the room: the append must stop */
            if (room >= 4) {
                memset(da, 'a', room); memset(dc, 'a', room);
                da[1] = 0; dc[1] = 0;
                ra = wia_strcatbuffa(da, "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ", room);
                rc = sys(dc, "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ", room);
                CHECK(ra == da && rc == dc, "append clipped by the bound: return");
                CHECK(memcmp(da, dc, room) == 0, "append clipped by the bound: identical buffers");
            }
        }
        VirtualFree(ba, 0, MEM_RELEASE);
        VirtualFree(bc, 0, MEM_RELEASE);
    }

    // ---- The caller lies about cch: both must fault, having written the same bytes ----------------
    // StrCatBuffA does not swallow this, measured, 37 of 37. So the comparison is whether each
    // side faulted and what each left behind, not a return value.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* ba = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        char* bc = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(ba && bc, "VirtualAlloc (lying-cch guards)");
        DWORD old;
        VirtualProtect(ba+pg, pg, PAGE_NOACCESS, &old);
        VirtualProtect(bc+pg, pg, PAGE_NOACCESS, &old);
        static char big[512];
        for (int i = 0; i < 400; ++i) big[i] = 'Z';
        big[400] = 0;
        int bothfaulted = 0, cases = 0;
        for (int room = 4; room <= 150; ++room) {
            char* da = (ba+pg) - room;
            char* dc = (bc+pg) - room;
            memset(da, 'a', room); memset(dc, 'a', room);
            da[2] = 0; dc[2] = 0;                    /* a valid short string, so the scan succeeds */
            int fa = 0, fc = 0;
            __try { wia_strcatbuffa(da, big, room + 300); } __except (EXCEPTION_EXECUTE_HANDLER) { fa = 1; }
            __try { sys(dc, big, room + 300); }        __except (EXCEPTION_EXECUTE_HANDLER) { fc = 1; }
            CHECK(fa == fc, "lying cch: both must fault, or neither");
            CHECK(memcmp(da, dc, room) == 0,
                  "lying cch: both must have written exactly the same bytes before faulting");
            if (memcmp(da, dc, room) != 0 && fails < 16) {
                int i = 0; while (i < room && da[i] == dc[i]) ++i;
                printf("   (room %d: first difference at byte %d, ours 0x%02X live 0x%02X)\n",
                       room, i, (unsigned char)da[i], (unsigned char)dc[i]);
            }
            if (fa && fc) ++bothfaulted;
            ++cases;
        }
        printf("  lying-cch sweep: %d of %d cases faulted on BOTH sides\n", bothfaulted, cases);
        CHECK(bothfaulted == cases, "every lying-cch case faulted on both sides");
        VirtualFree(ba, 0, MEM_RELEASE);
        VirtualFree(bc, 0, MEM_RELEASE);
    }

    // ---- randomized fuzz --------------------------------------------------------------------------
    {
        for (int t = 0; t < 300000; ++t) {
            int dn = rnd() % 120, sn = rnd() % 120, off = rnd() % 32;
            int cch = (int)(rnd() % 260) - 4;          /* includes negatives and zero */
            for (int i = 0; i < dn; ++i) { int v = 1 + (int)(rnd() % 255); ds[i] = (char)v; }
            ds[dn] = 0;
            for (int i = 0; i < sn; ++i) { int v = 1 + (int)(rnd() % 255); ss[i] = (char)v; }
            ss[sn] = 0;
            chk(ds, ss, cch, off, "fuzz");
        }
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (StrCatBuffA vs live shlwapi + oracle, WHOLE-BUFFER compare against "
           "poison -- required, because the distinctive rule is that an unterminated destination "
           "makes it write NOTHING AT ALL: exhaustive destination 0..40 x source 0..40 x cch 0..90 "
           "(150921 cases), negative and INT_MAX bounds, 40 alignments x exact-fit / one-short / "
           "room-to-spare, long strings to 400 including bounds that the destination already "
           "EXCEEDS, ALL 255 byte values in both strings, every NULL combination, 300k fuzz with "
           "negative bounds, a guard-page sweep proving the scan STOPS at cch in three shapes, and "
           "a LYING-cch sweep where both sides must FAULT -- this export does not swallow it -- "
           "having written exactly the same bytes first)\n");
    return 0;
}
