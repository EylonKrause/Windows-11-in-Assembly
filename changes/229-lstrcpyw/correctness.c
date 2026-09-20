// changes/229-lstrcpyw/correctness.c
// Gate 1: wia_lstrcpyw must be indistinguishable from kernelbase!lstrcpyW.
// Three-way: our ASM + wrapper vs the scalar oracle vs the LIVE export on this PC.
//
// The split-character sweep is the one this file exists for. lstrcpyW has no bound, so it runs off
// the end of a destination too small for the source, and the export returns NULL rather than
// faulting with the destination filled to its last writable character. If that destination has an
// ODD number of writable bytes the last character cannot be stored whole -- and probes/cpyw.c
// measured that the export writes whole characters only, never half of one.
//
// An implementation whose page clamp is in bytes rather than characters passes every ordinary test,
// returns the right NULL, and leaves one extra byte in the caller's buffer. Nothing crashes and no
// return value differs. Only an odd-aligned destination against a guard page can see it.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern wchar_t* wia_lstrcpyw(wchar_t*, const wchar_t*);
wchar_t* ref_lstrcpyw(wchar_t*, const wchar_t*);
typedef wchar_t* (WINAPI *FN)(wchar_t*, const wchar_t*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<15) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON 0x2A2A
#define DSZ 1024

static unsigned long sd = 0x2468u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

/* The guard-page buffers live in separate allocations, so their return pointers can never be equal
   on success. Compare the CLASSIFICATION, not the raw pointers. (That was the test bug change 227's
   harness shipped with.) */
static int same_ret(wchar_t* ra, wchar_t* da, wchar_t* rc, wchar_t* dc)
{
    if (ra == 0 || rc == 0) return ra == 0 && rc == 0;
    return ra == da && rc == dc;
}

static int chk(const wchar_t* src, int dstoff, const char* what)
{
    static wchar_t a[DSZ], b[DSZ], c[DSZ];
    for (int i = 0; i < DSZ; ++i) { a[i] = POISON; b[i] = POISON; c[i] = POISON; }
    wchar_t* da = a + dstoff; wchar_t* db = b + dstoff; wchar_t* dc = c + dstoff;
    wchar_t* ra = wia_lstrcpyw(da, src);
    wchar_t* rb = ref_lstrcpyw(db, src);
    wchar_t* rc = sys(dc, src);
    int ok = (ra == da) && (rb == db) && (rc == dc)
          && memcmp(a, b, sizeof a) == 0 && memcmp(a, c, sizeof a) == 0;
    if (!ok && fails < 15)
        printf("FAIL: %s -- src len %d, dst offset %d\n", what, (int)wcslen(src), dstoff);
    if (!ok) ++fails;
    return ok;
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h,"lstrcpyW");
    if (!sys) { HMODULE h2 = LoadLibraryW(L"kernel32.dll");
                sys = h2 ? (FN)GetProcAddress(h2,"lstrcpyW") : 0; }
    if(!sys){ printf("CORRECTNESS: cannot resolve lstrcpyW\n"); return 1; }

    static wchar_t s[2048];

    // ---- lengths x source alignment x destination alignment --------------------------------------
    for (int soff = 0; soff < 20; soff += 3) {
        for (int doff = 0; doff < 20; doff += 3) {
            for (int n = 0; n <= 150; ++n) {
                wchar_t* p = s + soff;
                for (int i = 0; i < n; ++i) p[i] = (wchar_t)(L'a' + i % 23);
                p[n] = 0;
                chk(p, doff, "length x src alignment x dst alignment");
            }
            for (int n = 151; n <= 600; n += 17) {
                wchar_t* p = s + soff;
                for (int i = 0; i < n; ++i) p[i] = (wchar_t)(L'a' + i % 23);
                p[n] = 0;
                chk(p, doff, "long, x alignments");
            }
        }
    }

    // ---- every code unit value, at three positions, surrogates included ---------------------------
    {
        static wchar_t b[64];
        for (int v = 1; v < 65536; ++v) {
            b[0]=(wchar_t)v; b[1]=L'b'; b[2]=L'c'; b[3]=0;   chk(b, 0, "code unit first");
            b[0]=L'a'; b[1]=L'b'; b[2]=(wchar_t)v; b[3]=L'd'; b[4]=L'e'; b[5]=0;
            chk(b, 0, "code unit middle");
            b[0]=L'a'; b[1]=(wchar_t)v; b[2]=0;              chk(b, 0, "code unit before the NUL");
        }
        /* and long runs of one value, to drive the 64-byte path */
        for (int v = 1; v < 65536; v += 97) {
            for (int i = 0; i < 200; ++i) s[i] = (wchar_t)v;
            s[200] = 0;
            chk(s, 0, "200 copies of one code unit");
        }
    }

    // ---- NULLs ------------------------------------------------------------------------------------
    {
        static wchar_t a[DSZ], c[DSZ];
        for (int i = 0; i < DSZ; ++i) { a[i] = POISON; c[i] = POISON; }
        memcpy(a, L"keepme", 7*sizeof(wchar_t));
        memcpy(c, L"keepme", 7*sizeof(wchar_t));
        CHECK(wia_lstrcpyw(a, 0) == 0, "NULL source returns NULL");
        CHECK(sys(c, 0) == 0,          "NULL source returns NULL (live)");
        CHECK(memcmp(a, c, sizeof a) == 0, "NULL source leaves the destination byte-identical");
        CHECK(wcscmp(a, L"keepme") == 0,   "NULL source leaves the destination ALONE");
        CHECK(wia_lstrcpyw(0, L"abc") == 0, "NULL destination returns NULL");
        CHECK(sys(0, L"abc") == 0,          "NULL destination returns NULL (live)");
        CHECK(wia_lstrcpyw(0, 0) == 0,      "both NULL return NULL");
        CHECK(sys(0, 0) == 0,               "both NULL return NULL (live)");
    }

    // ---- Guard page on the source: unterminated, at every distance --------------------------------
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != 0, "VirtualAlloc (source guard)");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static wchar_t da[4096], dc[4096];
        for (int tail = 1; tail <= 200; ++tail) {
            wchar_t* src = (wchar_t*)(base+pg) - tail;
            for (int i = 0; i < tail; ++i) src[i] = (wchar_t)(L'a' + i % 23);  /* NO terminator */
            for (int doff = 0; doff < 4; ++doff) {
                for (int i = 0; i < 4096; ++i) { da[i] = POISON; dc[i] = POISON; }
                wchar_t* ra = wia_lstrcpyw(da + doff, src);
                wchar_t* rc = sys(dc + doff, src);
                CHECK(same_ret(ra, da + doff, rc, dc + doff), "source guard: same return");
                CHECK(memcmp(da, dc, sizeof da) == 0,
                      "source guard: the PARTIAL COPY must be byte-identical to the live export");
            }
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    // ---- Guard page on the destination: too small, at every aligned room --------------------------
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* ba = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        char* bc = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(ba && bc, "VirtualAlloc (destination guards)");
        DWORD old;
        VirtualProtect(ba+pg, pg, PAGE_NOACCESS, &old);
        VirtualProtect(bc+pg, pg, PAGE_NOACCESS, &old);
        for (int srclen = 40; srclen <= 400; srclen += 41) {
            for (int i = 0; i < srclen; ++i) s[i] = (wchar_t)(L'A' + i % 26);
            s[srclen] = 0;
            for (int room = 1; room <= 200; ++room) {
                if (room > srclen) break;
                wchar_t* da = (wchar_t*)(ba+pg) - room;
                wchar_t* dc = (wchar_t*)(bc+pg) - room;
                for (int i = 0; i < room; ++i) { da[i] = POISON; dc[i] = POISON; }
                wchar_t* ra = wia_lstrcpyw(da, s);
                wchar_t* rc = sys(dc, s);
                CHECK(same_ret(ra, da, rc, dc), "destination guard: same return");
                CHECK(memcmp(da, dc, (size_t)room*sizeof(wchar_t)) == 0,
                      "destination guard: filled to the SAME character as the live export");
            }
        }
        VirtualFree(ba, 0, MEM_RELEASE);
        VirtualFree(bc, 0, MEM_RELEASE);
    }

    // ---- The split character: an odd number of writable bytes -------------------------------------
    // The sweep that catches a byte-granular page clamp. The export writes whole characters only,
    // so with 2n+1 writable bytes exactly 2n of them change. A clamp that rounds in bytes leaves
    // one extra byte behind, and nothing else in this file can see that.
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
            for (int i = 0; i < srclen; ++i) s[i] = (wchar_t)(L'A' + i % 26);
            s[srclen] = 0;
            for (int bytes = 1; bytes <= 201; ++bytes) {      /* EVERY width, odd and even */
                char* da = (ba+pg) - bytes;
                char* dc = (bc+pg) - bytes;
                memset(da, 0x5A, bytes);
                memset(dc, 0x5A, bytes);
                wchar_t* ra = wia_lstrcpyw((wchar_t*)da, s);
                wchar_t* rc = sys((wchar_t*)dc, s);
                CHECK(same_ret(ra, (wchar_t*)da, rc, (wchar_t*)dc),
                      "split character: same return");
                CHECK(memcmp(da, dc, bytes) == 0,
                      "split character: the SAME bytes changed as the live export");
                if (memcmp(da, dc, bytes) != 0 && fails < 16) {
                    int i = 0; while (i < bytes && da[i] == dc[i]) ++i;
                    printf("   (srclen %d, %d writable byte(s): first difference at byte %d, "
                           "ours 0x%02X live 0x%02X)\n", srclen, bytes, i,
                           (unsigned char)da[i], (unsigned char)dc[i]);
                }
            }
        }
        VirtualFree(ba, 0, MEM_RELEASE);
        VirtualFree(bc, 0, MEM_RELEASE);
    }

    // ---- randomized fuzz --------------------------------------------------------------------------
    {
        for (int t = 0; t < 150000; ++t) {
            int n = rnd() % 400;
            int soff = rnd() % 20, doff = rnd() % 20;
            wchar_t* p = s + soff;
            for (int i = 0; i < n; ++i) p[i] = (wchar_t)(1 + (rnd() % 65535));
            p[n] = 0;
            chk(p, doff, "fuzz");
        }
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (lstrcpyW vs live kernelbase + oracle, WHOLE-BUFFER compare against "
           "poison: lengths 0..600 x 7 source alignments x 7 destination alignments, ALL 65535 "
           "non-zero code unit values at three positions (surrogates included) and as 200-character "
           "runs, every NULL combination, 150k fuzz, and THREE guard-page sweeps -- an unterminated "
           "SOURCE at every distance 1..200, a DESTINATION too small at every room, and THE SPLIT "
           "CHARACTER: every destination width from 1 to 201 BYTES, odd and even, because the "
           "export writes whole characters only and a clamp that rounds in bytes rather than "
           "characters leaves one extra byte behind that nothing else here can see)\n");
    return 0;
}
