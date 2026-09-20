/* changes/205-uuidfromstring/probes/ufs.c
   Pin down rpcrt4!UuidFromStringA and UuidFromStringW before writing any assembly.

   Why this pair: the A form measures 84.14 ns against the W form's 25.31 ns for identical work,
   which is the signature of a narrow wrapper that widens its input and calls the wide path. This
   project's own wide GUID parser (change 118, ntdll!RtlGUIDFromString) does the same job in ~12 ns.

   Nothing about the contract is assumed. What has to be settled:
     * the accepted form, braced, unbraced, or either;
     * exact return codes for success and for every kind of malformed input (RPC_S_OK = 0,
       RPC_S_INVALID_STRING_UUID = 1700);
     * whether a NULL string means "nil UUID" (rpcrt4 has historically done this);
     * case sensitivity of the hex digits;
     * what happens after the 36th character, is a trailing NUL required, is trailing junk
       rejected, is a shorter string rejected;
     * whether the output UUID is touched at all on failure.                                     */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG (WINAPI *FNA)(unsigned char*, GUID*);
typedef LONG (WINAPI *FNW)(unsigned short*, GUID*);
static FNA A;
static FNW W;

#define POISON 0x5A

static void dump(const GUID* g){
    const unsigned char* p = (const unsigned char*)g;
    for (int i = 0; i < 16; ++i) printf("%02X", p[i]);
}

static void tryA(const char* s, const char* tag){
    GUID g; memset(&g, POISON, sizeof g);
    LONG r = A((unsigned char*)s, &g);
    printf("  A %-42s ret=%-5ld guid=", tag, r); dump(&g); printf("\n");
}
static void tryW(const wchar_t* s, const char* tag){
    GUID g; memset(&g, POISON, sizeof g);
    LONG r = W((unsigned short*)s, &g);
    printf("  W %-42s ret=%-5ld guid=", tag, r); dump(&g); printf("\n");
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"rpcrt4.dll");
    A = (FNA)GetProcAddress(h,"UuidFromStringA");
    W = (FNW)GetProcAddress(h,"UuidFromStringW");
    if(!A || !W){ printf("missing export(s)\n"); return 1; }
    printf("A=%p W=%p\n\n", (void*)A, (void*)W);

    printf("=== accepted forms ===\n");
    tryA("deadbeef-1234-5678-9abc-def011223344", "canonical lower, unbraced");
    tryW(L"deadbeef-1234-5678-9abc-def011223344", "canonical lower, unbraced");
    tryA("DEADBEEF-1234-5678-9ABC-DEF011223344", "canonical UPPER, unbraced");
    tryW(L"DEADBEEF-1234-5678-9ABC-DEF011223344", "canonical UPPER, unbraced");
    tryA("DeAdBeEf-1234-5678-9aBc-DeF011223344", "MiXeD case");
    tryA("{deadbeef-1234-5678-9abc-def011223344}", "BRACED");
    tryW(L"{deadbeef-1234-5678-9abc-def011223344}", "BRACED");
    tryA("00000000-0000-0000-0000-000000000000", "all zero");
    tryA("ffffffff-ffff-ffff-ffff-ffffffffffff", "all f");

    printf("\n=== NULL and empty ===\n");
    {
        GUID g; memset(&g, POISON, sizeof g);
        LONG r = A(NULL, &g);
        printf("  A NULL string                              ret=%-5ld guid=", r); dump(&g); printf("\n");
        memset(&g, POISON, sizeof g);
        r = W(NULL, &g);
        printf("  W NULL string                              ret=%-5ld guid=", r); dump(&g); printf("\n");
    }
    tryA("", "empty string");
    tryW(L"", "empty string");

    printf("\n=== malformed: does the output get touched? ===\n");
    tryA("deadbeef-1234-5678-9abc-def01122334",  "35 chars (one short)");
    tryA("deadbeef-1234-5678-9abc-def0112233445","37 chars (one long)");
    tryA("deadbeef-1234-5678-9abc-def01122334g", "bad hex digit at the end");
    tryA("geadbeef-1234-5678-9abc-def011223344", "bad hex digit at the start");
    tryA("deadbeef+1234-5678-9abc-def011223344", "wrong separator at 8");
    tryA("deadbeef-1234+5678-9abc-def011223344", "wrong separator at 13");
    tryA("deadbeef-1234-5678-9abc+def011223344", "wrong separator at 23");
    tryA("deadbeef 1234 5678 9abc def011223344", "spaces for separators");
    tryA("deadbeef-1234-5678-9abc-def011223344 ", "trailing space");
    tryA("deadbeef-1234-5678-9abc-def011223344x", "trailing junk char");
    tryA("  deadbeef-1234-5678-9abc-def011223344", "leading spaces");

    printf("\n=== is the 37th byte required to be NUL? ===\n");
    {
        /* build a 36-char uuid with deliberate garbage after it, no terminator inside 36 */
        char buf[64];
        memcpy(buf, "deadbeef-1234-5678-9abc-def011223344", 36);
        memcpy(buf + 36, "GARBAGE", 8);          /* NUL only at [43] */
        GUID g; memset(&g, POISON, sizeof g);
        LONG r = A((unsigned char*)buf, &g);
        printf("  A 36 chars then \"GARBAGE\"                  ret=%-5ld guid=", r); dump(&g); printf("\n");
    }

    printf("\n=== do A and W agree, character for character? ===\n");
    {
        unsigned long sd = 0x205205u;
        int diff = 0, checked = 0;
        for (int t = 0; t < 200000; ++t) {
            char a[64]; wchar_t w[64];
            static const char HEX[] = "0123456789abcdefABCDEF";
            int n = 36;
            for (int i = 0; i < 36; ++i) {
                sd = sd*1103515245u + 12345u;
                a[i] = HEX[(sd >> 16) % 22];
            }
            a[8] = a[13] = a[18] = a[23] = '-';
            /* corrupt some of them on purpose so failure paths are compared too */
            sd = sd*1103515245u + 12345u;
            if ((sd >> 16) % 4 == 0) { a[(sd >> 8) % 36] = (char)('!' + ((sd >> 3) % 60)); }
            sd = sd*1103515245u + 12345u;
            if ((sd >> 16) % 8 == 0) n = 30 + ((sd >> 5) % 8);
            a[n] = 0;
            for (int i = 0; i <= n; ++i) w[i] = (wchar_t)(unsigned char)a[i];
            GUID ga, gw; memset(&ga, POISON, sizeof ga); memset(&gw, POISON, sizeof gw);
            LONG ra = A((unsigned char*)a, &ga);
            LONG rw = W((unsigned short*)w, &gw);
            ++checked;
            if (ra != rw || memcmp(&ga, &gw, 16) != 0) {
                if (diff < 6) printf("  DIFFER on \"%s\": A ret=%ld  W ret=%ld\n", a, ra, rw);
                ++diff;
            }
        }
        printf("  %d generated strings: %d A/W differences\n", checked, diff);
    }

    printf("\n=== cost ===\n");
    {
        LARGE_INTEGER f,s,e; QueryPerformanceFrequency(&f);
        SetThreadAffinityMask(GetCurrentThread(),(DWORD_PTR)1<<2);
        SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS);
        static const char  SA[] = "deadbeef-1234-5678-9abc-def011223344";
        static const wchar_t SW[] = L"deadbeef-1234-5678-9abc-def011223344";
        GUID g; volatile LONG sink = 0;
        const int N = 300000;
        for (int i=0;i<3000;i++) sink ^= A((unsigned char*)SA,&g);
        double best = 1e300;
        for (int t=0;t<7;t++){
            QueryPerformanceCounter(&s);
            for(int i=0;i<N;i++) sink ^= A((unsigned char*)SA,&g);
            QueryPerformanceCounter(&e);
            double ns=(double)(e.QuadPart-s.QuadPart)*1e9/(double)f.QuadPart/N;
            if(ns<best)best=ns;
        }
        printf("  UuidFromStringA: %.2f ns\n", best);
        best = 1e300;
        for (int t=0;t<7;t++){
            QueryPerformanceCounter(&s);
            for(int i=0;i<N;i++) sink ^= W((unsigned short*)SW,&g);
            QueryPerformanceCounter(&e);
            double ns=(double)(e.QuadPart-s.QuadPart)*1e9/(double)f.QuadPart/N;
            if(ns<best)best=ns;
        }
        printf("  UuidFromStringW: %.2f ns   (sink=%ld)\n", best, sink);
    }
    return 0;
}
