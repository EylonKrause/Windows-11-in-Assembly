// changes/208-uuidfromstringw/correctness.c
// Gate 1: wia_uuidfromstringw must be indistinguishable from rpcrt4!UuidFromStringW.
//
// Three-way: our assembly vs the scalar oracle vs the LIVE export on this PC.
//
// Three things get special attention, and all three are specific to the wide form.
//
// 1. The output must be UNTOUCHED on failure, so every case pre-poisons the GUID and compares all
//    sixteen bytes afterwards even when the call is expected to fail.
// 2. The implementation narrows the 36 UTF-16 cells to bytes with a SATURATING vpackuswb before
//    parsing. That is only sound because 0100h-7FFFh clamp to 0FFh and 8000h-FFFFh clamp to 00h,
//    both invalid in the hex table, and because nothing but 002Dh can become '-'. So there is a
//    dedicated sweep over characters ABOVE 0xFF at every position, including U+0130 and U+FF21,
//    which a naive truncation would accept as '0' and '!', and U+802D and U+FF2D, which a careless
//    narrowing could turn into a separator.
// 3. The fast path reads 80 bytes before the length is known, guarded by a page-offset check whose
//    fallback narrows scalar-wise instead. The page-guard section places strings of every length at
//    a PAGE_NOACCESS boundary so a wrong guard faults rather than merely disagreeing.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern long wia_uuidfromstringw(wchar_t*, GUID*);
long ref_uuidfromstringw(wchar_t*, GUID*);
typedef long (WINAPI *FN)(wchar_t*, GUID*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON 0x5A

static int one(const wchar_t* s){
    GUID a, b, c;
    memset(&a, POISON, sizeof a); memset(&b, POISON, sizeof b); memset(&c, POISON, sizeof c);
    long ra = wia_uuidfromstringw((wchar_t*)s, &a);
    long rb = ref_uuidfromstringw((wchar_t*)s, &b);
    long rc = sys((wchar_t*)s, &c);
    if (ra != rb || ra != rc) return 0;
    if (memcmp(&a, &b, 16) != 0 || memcmp(&a, &c, 16) != 0) return 0;
    return 1;
}

static unsigned long sd = 0x208208u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

static const wchar_t GOOD[] = L"deadbeef-1234-5678-9abc-def011223344";

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"rpcrt4.dll");
    sys = (FN)GetProcAddress(h, "UuidFromStringW");
    if(!sys){ printf("CORRECTNESS: cannot resolve rpcrt4!UuidFromStringW\n"); return 1; }

    // ---- the NULL pointer is a SUCCESS case ----
    {
        GUID a, c;
        memset(&a, POISON, sizeof a); memset(&c, POISON, sizeof c);
        long ra = wia_uuidfromstringw(NULL, &a);
        long rc = sys(NULL, &c);
        CHECK(ra == rc, "NULL string: same return");
        CHECK(memcmp(&a, &c, 16) == 0, "NULL string: same output (the nil uuid)");
        CHECK(ra == 0, "NULL string returns RPC_S_OK");
    }

    // ---- hand-picked shapes ----
    {
        static const wchar_t* S[] = {
            L"deadbeef-1234-5678-9abc-def011223344",
            L"DEADBEEF-1234-5678-9ABC-DEF011223344",
            L"DeAdBeEf-1234-5678-9aBc-DeF011223344",
            L"00000000-0000-0000-0000-000000000000",
            L"ffffffff-ffff-ffff-ffff-ffffffffffff",
            L"{deadbeef-1234-5678-9abc-def011223344}",  /* braced: REJECTED */
            L"{deadbeef-1234-5678-9abc-def011223344",
            L"deadbeef-1234-5678-9abc-def011223344}",
            L"",
            L"-",
            L"deadbeef",
            L"deadbeef-1234-5678-9abc-def01122334",     /* one short */
            L"deadbeef-1234-5678-9abc-def0112233445",   /* one long  */
            L"deadbeef-1234-5678-9abc-def01122334g",
            L"geadbeef-1234-5678-9abc-def011223344",
            L"deadbeef+1234-5678-9abc-def011223344",
            L"deadbeef-1234+5678-9abc-def011223344",
            L"deadbeef-1234-5678+9abc-def011223344",
            L"deadbeef-1234-5678-9abc+def011223344",
            L"deadbeef 1234 5678 9abc def011223344",
            L" deadbeef-1234-5678-9abc-def011223344",
            L"deadbeef-1234-5678-9abc-def011223344 ",
            L"deadbeef-1234-5678-9abc-def011223344x",
        };
        for (int i = 0; i < (int)(sizeof(S)/sizeof(S[0])); ++i)
            CHECK(one(S[i]), "hand-picked shape");
    }

    // ---- every single-character corruption over the whole low 255, at every position ----
    for (int pos = 0; pos < 36; ++pos) {
        for (int v = 1; v < 256; ++v) {
            wchar_t buf[48];
            memcpy(buf, GOOD, sizeof(GOOD));
            buf[pos] = (wchar_t)v;
            CHECK(one(buf), "single-character corruption sweep (low 255)");
        }
    }

    // ---- The wide sweep: characters above 0xFF at every position. This is what proves the
    // ---- saturating narrow is sound; a truncation would read U+0130 as '0'.
    {
        static const wchar_t WIDE[] = {
            0x0100, 0x0130, 0x0141, 0x0161, 0x01FF, 0x1234, 0x2D2D, 0x3030,
            0x7FFF, 0x8000, 0x802D, 0x8030, 0xFF10, 0xFF21, 0xFF2D, 0xFF41, 0xFFFF
        };
        for (int pos = 0; pos < 36; ++pos)
            for (int k = 0; k < (int)(sizeof(WIDE)/sizeof(WIDE[0])); ++k) {
                wchar_t buf[48];
                memcpy(buf, GOOD, sizeof(GOOD));
                buf[pos] = WIDE[k];
                CHECK(one(buf), "wide-character sweep (above 0xFF, incl. 802Dh and FF2Dh)");
            }
    }

    // ---- every truncation length 0..40 ----
    {
        static const wchar_t LONGER[] = L"deadbeef-1234-5678-9abc-def011223344xxxx";
        for (int n = 0; n <= 40; ++n) {
            wchar_t buf[48];
            memcpy(buf, LONGER, sizeof(LONGER));
            buf[n] = 0;
            CHECK(one(buf), "truncation at every length 0..40");
        }
    }

    // ---- randomized fuzz ----
    {
        static const wchar_t HEX[] = L"0123456789abcdefABCDEF";
        for (int t = 0; t < 400000; ++t) {
            wchar_t buf[48];
            for (int i = 0; i < 36; ++i) buf[i] = HEX[rnd() % 22];
            buf[8] = buf[13] = buf[18] = buf[23] = L'-';
            unsigned k = rnd() % 10;
            if (k < 3) buf[rnd() % 36] = (wchar_t)(rnd() & 0xFFFF);
            if (k == 3) buf[(rnd() % 4) * 5 + 8] = (wchar_t)(rnd() & 0xFF);
            int n = 36;
            if (k == 4) n = rnd() % 40;
            buf[n] = 0;
            if (k == 5 && n < 40) { buf[n] = L'a'; buf[n+1] = 0; }
            CHECK(one(buf), "fuzz");
        }
    }

    // ---- PAGE GUARD: the string ends right at a PAGE_NOACCESS boundary ----
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != NULL, "VirtualAlloc");
        DWORD old; VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);
        static const wchar_t SRC[] = L"deadbeef-1234-5678-9abc-def011223344xx";
        for (int len = 0; len <= 38; ++len) {
            wchar_t* p = (wchar_t*)((base + pg) - (SIZE_T)(len + 1) * sizeof(wchar_t));
            for (int i = 0; i < len; ++i) p[i] = SRC[i];
            p[len] = 0;
            CHECK(one(p), "page-guard: string ends exactly at a NOACCESS page");
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (UuidFromStringW vs live rpcrt4 + oracle -- return value AND all 16 "
           "output bytes on EVERY case including the failing ones, since the contract leaves the "
           "GUID untouched on error: the NULL-pointer success case, 23 hand-picked shapes, an "
           "exhaustive 36-position x 255-value corruption sweep, a 36 x 17 WIDE sweep over "
           "characters above 0xFF (which is what proves the saturating vpackuswb narrow is sound -- "
           "it includes U+0130 and U+FF21, which a truncation would read as '0' and '!', and U+802D "
           "and U+FF2D, which a careless narrow could turn into a separator), every truncation "
           "length 0..40, 400k fuzz, and a PAGE_NOACCESS guard with every length 0..38 ending at "
           "the boundary -- which exercises the 80-byte page guard and its scalar-narrow fallback)\n");
    return 0;
}
