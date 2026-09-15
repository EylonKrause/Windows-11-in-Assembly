/* changes/207-iidfromstring/probes/iid.c
   Pin down combase!IIDFromString before writing any assembly.

   It costs 33.64 ns to parse 38 characters. This project already has two GUID parsers with DIFFERENT
   contracts -- ntdll!RtlGUIDFromString (change 118) requires braces, rpcrt4!UuidFromStringA (change
   205) rejects them -- so nothing here is inherited. What has to be settled:
     * braces required, optional, or rejected;
     * the exact HRESULT for each kind of malformed input;
     * whether NULL is tolerated, and what it means;
     * whether the output IID is touched on failure;
     * whether a trailing NUL is required right after the closing brace;
     * case sensitivity;
     * and whether CLSIDFromString behaves identically, since if it does one implementation covers
       both exports (as changes 198/200 did for their aliases).                                    */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *FN)(const wchar_t*, GUID*);
static FN IIDf, CLSIDf;

#define POISON 0x5A

static void dump(const GUID* g){
    const unsigned char* p = (const unsigned char*)g;
    for (int i = 0; i < 16; ++i) printf("%02X", p[i]);
}

static void t2(const wchar_t* s, const char* tag){
    GUID a, b; memset(&a, POISON, sizeof a); memset(&b, POISON, sizeof b);
    HRESULT ra = IIDf(s, &a);
    HRESULT rb = CLSIDf(s, &b);
    printf("  %-40s IID hr=%08lX guid=", tag, (unsigned long)ra); dump(&a);
    printf("  | CLSID hr=%08lX %s\n", (unsigned long)rb,
           (ra == rb && memcmp(&a,&b,16) == 0) ? "(same)" : "(DIFFERS)");
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"combase.dll");
    IIDf   = (FN)GetProcAddress(h, "IIDFromString");
    CLSIDf = (FN)GetProcAddress(h, "CLSIDFromString");
    if (!IIDf || !CLSIDf) {
        h = LoadLibraryW(L"ole32.dll");
        if (!IIDf)   IIDf   = (FN)GetProcAddress(h, "IIDFromString");
        if (!CLSIDf) CLSIDf = (FN)GetProcAddress(h, "CLSIDFromString");
    }
    if (!IIDf) { printf("no IIDFromString\n"); return 1; }
    printf("IIDFromString=%p  CLSIDFromString=%p\n\n", (void*)IIDf, (void*)CLSIDf);

    printf("=== accepted forms (IID | does CLSID agree?) ===\n");
    t2(L"{DEADBEEF-1234-5678-9ABC-DEF011223344}", "braced UPPER");
    t2(L"{deadbeef-1234-5678-9abc-def011223344}", "braced lower");
    t2(L"{DeAdBeEf-1234-5678-9aBc-DeF011223344}", "braced mixed");
    t2(L"deadbeef-1234-5678-9abc-def011223344",   "UNBRACED");
    t2(L"{00000000-0000-0000-0000-000000000000}", "braced all zero");
    t2(L"{FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF}", "braced all f");

    printf("\n=== malformed: is the output touched? ===\n");
    t2(L"",                                        "empty");
    t2(L"{}",                                      "just braces");
    t2(L"{DEADBEEF-1234-5678-9ABC-DEF01122334}",  "37 chars (one short)");
    t2(L"{DEADBEEF-1234-5678-9ABC-DEF0112233445}","39 chars (one long)");
    t2(L"{DEADBEEF-1234-5678-9ABC-DEF01122334g}",  "bad hex digit");
    t2(L"{GEADBEEF-1234-5678-9ABC-DEF011223344}",  "bad hex at the start");
    t2(L"{DEADBEEF+1234-5678-9ABC-DEF011223344}",  "wrong separator at 9");
    t2(L"{DEADBEEF-1234-5678-9ABC-DEF011223344",   "missing closing brace");
    t2(L"DEADBEEF-1234-5678-9ABC-DEF011223344}",   "missing opening brace");
    t2(L"{DEADBEEF-1234-5678-9ABC-DEF011223344}x", "trailing junk");
    t2(L" {DEADBEEF-1234-5678-9ABC-DEF011223344}", "leading space");
    t2(L"{DEADBEEF-1234-5678-9ABC-DEF011223344} ", "trailing space");

    printf("\n=== NULL string ===\n");
    {
        GUID a; memset(&a, POISON, sizeof a);
        HRESULT r = IIDf(NULL, &a);
        printf("  IIDFromString(NULL)   hr=%08lX guid=", (unsigned long)r); dump(&a); printf("\n");
        memset(&a, POISON, sizeof a);
        r = CLSIDf(NULL, &a);
        printf("  CLSIDFromString(NULL) hr=%08lX guid=", (unsigned long)r); dump(&a); printf("\n");
    }

    printf("\n=== does a NUL have to follow the closing brace? ===\n");
    {
        wchar_t buf[64];
        memcpy(buf, L"{DEADBEEF-1234-5678-9ABC-DEF011223344}", 38*sizeof(wchar_t));
        for (int i = 0; i < 8; ++i) buf[38+i] = L'Z';
        buf[46] = 0;
        GUID a; memset(&a, POISON, sizeof a);
        HRESULT r = IIDf(buf, &a);
        printf("  38 chars then \"ZZZZZZZZ\"   hr=%08lX guid=", (unsigned long)r); dump(&a); printf("\n");
    }

    printf("\n=== IID vs CLSID over a generated corpus ===\n");
    {
        static const wchar_t HEX[] = L"0123456789abcdefABCDEF";
        unsigned long sd = 0x207207u;
        int diff = 0, checked = 0;
        for (int t = 0; t < 200000; ++t) {
            wchar_t s[64];
            s[0] = L'{';
            for (int i = 0; i < 36; ++i) { sd = sd*1103515245u+12345u; s[1+i] = HEX[(sd>>16)%22]; }
            s[9] = s[14] = s[19] = s[24] = L'-';
            s[37] = L'}'; s[38] = 0;
            sd = sd*1103515245u+12345u;
            unsigned k = (sd >> 16) % 6;
            if (k == 0) s[1 + ((sd >> 5) % 36)] = (wchar_t)(L'!' + ((sd >> 9) % 60));
            if (k == 1) s[(sd >> 7) % 38] = L'?';
            if (k == 2) s[30 + ((sd >> 11) % 8)] = 0;
            GUID a, b; memset(&a, POISON, sizeof a); memset(&b, POISON, sizeof b);
            HRESULT ra = IIDf(s, &a), rb = CLSIDf(s, &b);
            ++checked;
            if (ra != rb || memcmp(&a, &b, 16) != 0) {
                if (diff < 5) printf("  DIFFER: IID=%08lX CLSID=%08lX  \"%ls\"\n",
                                     (unsigned long)ra, (unsigned long)rb, s);
                ++diff;
            }
        }
        printf("  %d strings: %d IID/CLSID differences\n", checked, diff);
    }

    printf("\n=== cost ===\n");
    {
        LARGE_INTEGER f,s,e; QueryPerformanceFrequency(&f);
        SetThreadAffinityMask(GetCurrentThread(),(DWORD_PTR)1<<2);
        SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS);
        static const wchar_t S[] = L"{DEADBEEF-1234-5678-9ABC-DEF011223344}";
        static const wchar_t B[] = L"{DEADBEEF-1234-5678-9ABC-DEF01122334g}";
        GUID g; volatile long sink = 0;
        const int N = 200000;
        for (int i=0;i<3000;i++) sink ^= (long)IIDf(S,&g);
        double best=1e300;
        for (int t=0;t<7;t++){
            QueryPerformanceCounter(&s);
            for(int i=0;i<N;i++) sink ^= (long)IIDf(S,&g);
            QueryPerformanceCounter(&e);
            double ns=(double)(e.QuadPart-s.QuadPart)*1e9/(double)f.QuadPart/N;
            if(ns<best)best=ns;
        }
        printf("  IIDFromString, valid:   %.2f ns\n", best);
        best=1e300;
        for (int t=0;t<7;t++){
            QueryPerformanceCounter(&s);
            for(int i=0;i<N;i++) sink ^= (long)IIDf(B,&g);
            QueryPerformanceCounter(&e);
            double ns=(double)(e.QuadPart-s.QuadPart)*1e9/(double)f.QuadPart/N;
            if(ns<best)best=ns;
        }
        printf("  IIDFromString, invalid: %.2f ns  (sink=%ld)\n", best, sink);
    }
    return 0;
}
