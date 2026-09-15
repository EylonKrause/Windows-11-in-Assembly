/* changes/206-stringfromguid2/probes/sfg.c
   Pin down combase!StringFromGUID2 before writing any assembly.

   It renders the same braced form change 202 already produces -- "{XXXXXXXX-XXXX-XXXX-XXXX-
   XXXXXXXXXXXX}" -- but its RETURN convention is different (a character count, not a Win32 error),
   and the buffer-too-small behaviour is a separate question. Nothing is assumed.

   What has to be settled:
     * the return value on success -- 39 (chars written including the NUL) or 38;
     * what happens when cchMax is exactly 39, 38, 1, 0, and negative;
     * whether the buffer is written at all when it is too small;
     * whether the hex is upper-case (it must match 202's output exactly for the renderer to be
       reusable verbatim);
     * whether a NULL buffer is tolerated;
     * the cost.                                                                                  */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int (WINAPI *FN)(const GUID*, wchar_t*, int);
static FN f;

#define PW ((wchar_t)0x2A2A)
#define DSZ 96

static void show(int cch, const char* tag){
    static wchar_t b[DSZ];
    for (int i = 0; i < DSZ; ++i) b[i] = PW;
    GUID g = {0xDEADBEEF,0x1234,0x5678,{0x9A,0xBC,0xDE,0xF0,0x11,0x22,0x33,0x44}};
    int r = f(&g, b, cch);
    printf("  cchMax=%-12d ret=%-6d buf=[", cch, r);
    for (int i = 0; i < 44; ++i) {
        wchar_t c = b[i];
        putchar(c == 0 ? '.' : (c == PW ? '-' : (c < 128 ? (char)c : '?')));
    }
    printf("]\n");
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"combase.dll");
    f = (FN)GetProcAddress(h, "StringFromGUID2");
    if (!f) { h = LoadLibraryW(L"ole32.dll"); f = (FN)GetProcAddress(h, "StringFromGUID2"); }
    if (!f) { printf("no StringFromGUID2 export\n"); return 1; }
    printf("StringFromGUID2 = %p\n\n", (void*)f);

    printf("=== cchMax sweep ('.' = NUL, '-' = untouched 0x2A2A) ===\n");
    show(64, "generous");
    show(40, "one spare");
    show(39, "exact fit");
    show(38, "one short");
    show(37, "two short");
    show(20, "half");
    show(2,  "two");
    show(1,  "one");
    show(0,  "zero");
    show(-1, "negative");
    show(-1000, "very negative");

    printf("\n=== case and exact characters ===\n");
    {
        static wchar_t b[DSZ];
        GUID g = {0xABCDEF01,0x2345,0x6789,{0xAB,0xCD,0xEF,0x01,0x23,0x45,0x67,0x89}};
        int r = f(&g, b, 64);
        printf("  ret=%d  \"%ls\"\n", r, b);
        GUID z = {0,0,0,{0,0,0,0,0,0,0,0}};
        r = f(&z, b, 64);
        printf("  ret=%d  \"%ls\"\n", r, b);
        GUID o; memset(&o, 0xFF, sizeof o);
        r = f(&o, b, 64);
        printf("  ret=%d  \"%ls\"\n", r, b);
    }

    printf("\n=== does it match change 202's ConvertGuidToStringW output byte for byte? ===\n");
    {
        HMODULE hi = LoadLibraryW(L"iphlpapi.dll");
        typedef DWORD (WINAPI *CG)(const GUID*, wchar_t*, DWORD);
        CG cg = (CG)GetProcAddress(hi, "ConvertGuidToStringW");
        if (!cg) { printf("  (iphlpapi unavailable)\n"); }
        else {
            unsigned long sd = 0x206206u;
            int diff = 0;
            static wchar_t a[DSZ], b[DSZ];
            for (int t = 0; t < 200000; ++t) {
                GUID g; unsigned char* p = (unsigned char*)&g;
                for (int i = 0; i < 16; ++i) { sd = sd*1103515245u+12345u; p[i] = (unsigned char)(sd>>16); }
                for (int i = 0; i < DSZ; ++i) { a[i] = PW; b[i] = PW; }
                int  ra = f(&g, a, 64);
                DWORD rb = cg(&g, b, 64);
                (void)ra; (void)rb;
                for (int i = 0; i < 40; ++i) if (a[i] != b[i]) { ++diff; break; }
            }
            printf("  200000 GUIDs: %d character differences vs ConvertGuidToStringW\n", diff);
        }
    }

    printf("\n=== NULL buffer ===\n");
    {
        GUID g = {0xDEADBEEF,0x1234,0x5678,{0x9A,0xBC,0xDE,0xF0,0x11,0x22,0x33,0x44}};
        /* guarded: if this faults, the probe dies here and that is itself the answer */
        printf("  about to call with a NULL buffer and cchMax 0...\n");
        int r = f(&g, NULL, 0);
        printf("  ret=%d (survived)\n", r);
    }

    printf("\n=== cost ===\n");
    {
        LARGE_INTEGER fq,s,e; QueryPerformanceFrequency(&fq);
        SetThreadAffinityMask(GetCurrentThread(),(DWORD_PTR)1<<2);
        SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS);
        static wchar_t b[DSZ];
        GUID g = {0xDEADBEEF,0x1234,0x5678,{0x9A,0xBC,0xDE,0xF0,0x11,0x22,0x33,0x44}};
        volatile int sink = 0;
        const int N = 400000;
        for (int i=0;i<4000;i++) sink ^= f(&g,b,64);
        double best = 1e300;
        for (int t=0;t<7;t++){
            QueryPerformanceCounter(&s);
            for(int i=0;i<N;i++) sink ^= f(&g,b,64);
            QueryPerformanceCounter(&e);
            double ns=(double)(e.QuadPart-s.QuadPart)*1e9/(double)fq.QuadPart/N;
            if(ns<best)best=ns;
        }
        printf("  StringFromGUID2, cchMax 64: %.2f ns  (sink=%d)\n", best, sink);
        best = 1e300;
        for (int t=0;t<7;t++){
            QueryPerformanceCounter(&s);
            for(int i=0;i<N;i++) sink ^= f(&g,b,20);
            QueryPerformanceCounter(&e);
            double ns=(double)(e.QuadPart-s.QuadPart)*1e9/(double)fq.QuadPart/N;
            if(ns<best)best=ns;
        }
        printf("  StringFromGUID2, cchMax 20 (too small): %.2f ns\n", best);
    }
    return 0;
}
