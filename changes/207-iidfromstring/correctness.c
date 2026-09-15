// changes/207-iidfromstring/correctness.c
// Gate 1: wia_iidfromstring must be indistinguishable from combase!IIDFromString.
//
// Three-way: our assembly vs the scalar oracle vs the LIVE export on this PC.
//
// THIS TEST IS MOSTLY ABOUT THE FAILURE PATH. IIDFromString writes into the caller's GUID as it
// parses, so a malformed string leaves a partially-filled GUID that must match byte for byte. Every
// case therefore starts from a poison fill and compares all sixteen output bytes AND the HRESULT --
// and the HRESULT itself is two-valued (E_INVALIDARG for a structural rejection, CO_E_IIDSTRING for a
// content rejection), so returning "an error" is not good enough either.
//
// The centrepiece is the single-character corruption sweep: 38 positions x 256 byte values, plus the
// same sweep with wide characters above 0xFF. That is the test that pins the partial-write table,
// because each position stops the parser at a different field boundary.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern long wia_iidfromstring(const wchar_t*, GUID*);
long ref_iidfromstring(const wchar_t*, GUID*);
typedef HRESULT (WINAPI *FN)(const wchar_t*, GUID*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON 0x5A

static const wchar_t GOOD[] = L"{DEADBEEF-1234-5678-9ABC-DEF011223344}";

/* HRESULT AND all sixteen bytes, three ways, from a poisoned baseline */
static int one(const wchar_t* s){
    GUID a, b, c;
    memset(&a, POISON, sizeof a); memset(&b, POISON, sizeof b); memset(&c, POISON, sizeof c);
    long ra = wia_iidfromstring(s, &a);
    long rb = ref_iidfromstring(s, &b);
    long rc = (long)sys(s, &c);
    if (ra != rb || ra != rc) return 0;
    if (memcmp(&a, &b, 16) != 0 || memcmp(&a, &c, 16) != 0) return 0;
    return 1;
}

static unsigned long sd = 0x207207u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"combase.dll");
    sys = (FN)GetProcAddress(h, "IIDFromString");
    if (!sys) { h = LoadLibraryW(L"ole32.dll"); sys = (FN)GetProcAddress(h, "IIDFromString"); }
    if (!sys) { printf("CORRECTNESS: cannot resolve IIDFromString\n"); return 1; }

    // ---- NULL output pointer: E_INVALIDARG, and nothing may be dereferenced ----
    {
        long ra = wia_iidfromstring(GOOD, NULL);
        long rc = (long)sys(GOOD, NULL);
        CHECK(ra == rc, "NULL lpiid: same HRESULT");
        CHECK(ra == (long)0x80070057L, "NULL lpiid is E_INVALIDARG");
        ra = wia_iidfromstring(NULL, NULL);
        rc = (long)sys(NULL, NULL);
        CHECK(ra == rc, "both NULL");
    }

    // ---- NULL string is SUCCESS with the nil GUID ----
    CHECK(one(NULL), "NULL lpsz -> nil GUID, S_OK");

    // ---- the valid baseline, and case variations ----
    {
        static const wchar_t* S[] = {
            L"{DEADBEEF-1234-5678-9ABC-DEF011223344}",
            L"{deadbeef-1234-5678-9abc-def011223344}",
            L"{DeAdBeEf-1234-5678-9aBc-DeF011223344}",
            L"{00000000-0000-0000-0000-000000000000}",
            L"{FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF}",
            L"{ffffffff-ffff-ffff-ffff-ffffffffffff}",
            L"{01234567-89AB-CDEF-0123-456789ABCDEF}",
        };
        for (int i = 0; i < 7; ++i) CHECK(one(S[i]), "valid forms");
    }

    // ---- structural rejections: the length must be EXACTLY 38 -> E_INVALIDARG, nothing written ----
    {
        static const wchar_t* S[] = {
            L"",
            L"{}",
            L"{DEADBEEF-1234-5678-9ABC-DEF011223344",     /* 37, no closing brace */
            L"{DEADBEEF-1234-5678-9ABC-DEF0112233445}",   /* 39 */
            L"{DEADBEEF-1234-5678-9ABC-DEF01122334}",     /* 37 */
            L"DEADBEEF-1234-5678-9ABC-DEF011223344",      /* 36, unbraced */
            L"{DEADBEEF-1234-5678-9ABC-DEF011223344}x",   /* 39, trailing junk */
            L" {DEADBEEF-1234-5678-9ABC-DEF011223344}",   /* 39, leading space */
            L"{DEADBEEF-1234-5678-9ABC-DEF011223344} ",   /* 39, trailing space */
            L"{",
        };
        for (int i = 0; i < 10; ++i) CHECK(one(S[i]), "structural rejection (length != 38)");
    }

    // ---- THE CORRUPTION SWEEP: 38 positions x 256 low values. This is what pins the
    // ---- partial-write table, because each position stops the parser at a different boundary.
    for (int pos = 0; pos < 38; ++pos) {
        for (int v = 1; v < 256; ++v) {          /* v == 0 would shorten the string; covered above */
            wchar_t s[64];
            memcpy(s, GOOD, sizeof(GOOD));
            s[pos] = (wchar_t)v;
            CHECK(one(s), "single-character corruption sweep (low 255)");
        }
    }

    // ---- the same sweep with characters above 0xFF, which must not index a 256-entry table ----
    for (int pos = 0; pos < 38; ++pos) {
        static const wchar_t WIDE[] = { 0x0100, 0x0130, 0x0141, 0x0161, 0x1234, 0x2D2D, 0x7B7B,
                                        0xFF10, 0xFF21, 0xFF41, 0xFFFF, 0x8000 };
        for (int k = 0; k < 12; ++k) {
            wchar_t s[64];
            memcpy(s, GOOD, sizeof(GOOD));
            s[pos] = WIDE[k];
            CHECK(one(s), "wide-character corruption sweep (above 0xFF)");
        }
    }

    // ---- two corruptions at once: the FIRST one must decide where the parser stops ----
    for (int p1 = 0; p1 < 38; p1 += 3) {
        for (int p2 = 0; p2 < 38; p2 += 5) {
            wchar_t s[64];
            memcpy(s, GOOD, sizeof(GOOD));
            s[p1] = L'Z'; s[p2] = L'Q';
            CHECK(one(s), "two corruptions: the earlier one wins");
        }
    }

    // ---- fuzz: random hex bodies, random corruption, random truncation ----
    {
        static const wchar_t HEX[] = L"0123456789abcdefABCDEF";
        for (int t = 0; t < 400000; ++t) {
            wchar_t s[64];
            s[0] = L'{';
            for (int i = 0; i < 36; ++i) s[1+i] = HEX[rnd() % 22];
            s[9] = s[14] = s[19] = s[24] = L'-';
            s[37] = L'}'; s[38] = 0;
            unsigned k = rnd() % 8;
            if (k == 0) s[rnd() % 38] = (wchar_t)(rnd() & 0xFF);
            if (k == 1) s[rnd() % 38] = (wchar_t)(rnd() & 0xFFFF);
            if (k == 2) { int n = 20 + (int)(rnd() % 20); s[n] = 0; }
            if (k == 3) { s[38] = L'x'; s[39] = 0; }
            CHECK(one(s), "fuzz");
        }
    }

    // ---- PAGE GUARD: strings of every length ending exactly at a NOACCESS page ----
    // The fast path reads 80 bytes before the length is known, so this is where a wrong page-offset
    // guard or a miscounted fallback walk faults instead of merely disagreeing.
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != NULL, "VirtualAlloc");
        DWORD old; VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);
        for (int len = 0; len <= 42; ++len) {
            wchar_t* p = (wchar_t*)((base + pg) - (SIZE_T)(len + 1) * sizeof(wchar_t));
            for (int i = 0; i < len; ++i) p[i] = (i < 38) ? GOOD[i] : L'x';
            p[len] = 0;
            CHECK(one(p), "page-guard: string ends exactly at a NOACCESS page");
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (IIDFromString vs live combase + oracle -- the HRESULT (which is "
           "two-valued: E_INVALIDARG for a structural rejection, CO_E_IIDSTRING for a content one) "
           "AND all 16 output bytes from a poisoned baseline on EVERY case, because this function "
           "writes into the caller's GUID as it parses and a malformed string must leave exactly the "
           "same PARTIAL GUID: NULL output and NULL string, 7 valid forms, 10 structural rejections, "
           "a 38-position x 255-value single-character corruption sweep plus 38 x 12 wide characters "
           "above 0xFF (this is what pins the partial-write table, since each position stops the "
           "parser at a different field boundary), double corruptions proving the earlier one wins, "
           "400k fuzz, and a NOACCESS page-guard with every length 0..42 ending at the boundary)\n");
    return 0;
}
