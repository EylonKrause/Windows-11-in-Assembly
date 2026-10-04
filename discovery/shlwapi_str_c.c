/* discovery/shlwapi_str_c.c
   The shlwapi Str* exports still unconverted that are NOT the linguistic (StrCmp/StrCmpI) family:
   the "C collation" comparisons, the copy/append trio, and the integer parsers.

   WHY. Earlier sweeps converted the scanning half of this family (131-139, 169, 214-218) and ruled
   the linguistic half out on evidence (strcmpn_is_linguistic.c). What is left was never timed. The
   "C" comparisons are documented as C-run-time ASCII collation, which if true is ordinal and
   reproducible; StrCpyW / StrCatW / StrNCatW are the kind of thin wrapper that change 302 just found
   walking a string twice; the parsers are short-input functions where call overhead decides.

   Every row is timed next to the C runtime function doing the same job, because the question is not
   "is it slow" but "is there a gap that is implementation rather than workload".
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>
#include <stdlib.h>

typedef int    (WINAPI *PCMP)(PCWSTR, PCWSTR);
typedef int    (WINAPI *PCMPN)(PCWSTR, PCWSTR, int);
typedef int    (WINAPI *PCMPA)(PCSTR, PCSTR);
typedef PWSTR  (WINAPI *PCPY)(PWSTR, PCWSTR);
typedef PWSTR  (WINAPI *PNCAT)(PWSTR, PCWSTR, int);
typedef int    (WINAPI *PTOI)(PCWSTR);
typedef int    (WINAPI *PTOIA)(PCSTR);
typedef BOOL   (WINAPI *PTOIEX)(PCWSTR, DWORD, int*);
typedef BOOL   (WINAPI *PTOI64)(PCWSTR, DWORD, LONGLONG*);

static volatile uint64_t sink;
static double freq_(void) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)f.QuadPart; }

#define TIME(LABEL, CHARS, BODY)                                                                     \
    do { double fq = freq_(); int inner = 16; double best = 1e300;                                    \
         for (int w = 0; w < 16; ++w) { BODY; }                                                       \
         for (;;) { LARGE_INTEGER q0_,q1_; QueryPerformanceCounter(&q0_); for (int i=0;i<inner;++i){BODY;}  \
                    QueryPerformanceCounter(&q1_); double ns=(double)(q1_.QuadPart-q0_.QuadPart)*1e9/fq;     \
                    if (ns >= 200000.0 || inner >= (1<<22)) break; inner *= 4; }                      \
         for (int t = 0; t < 120; ++t) { LARGE_INTEGER q0_,q1_; QueryPerformanceCounter(&q0_);              \
                    for (int i=0;i<inner;++i){BODY;} QueryPerformanceCounter(&q1_);                      \
                    double ns=(double)(q1_.QuadPart-q0_.QuadPart)*1e9/fq/inner; if (ns<best) best=ns; }    \
         printf("  %-36s %9.2f ns  %7.3f ns/char\n", LABEL, best, (CHARS) ? best / (CHARS) : 0.0);     \
    } while (0)

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE s = LoadLibraryW(L"shlwapi.dll");
    PCMP   CmpC   = (PCMP)  GetProcAddress(s, "StrCmpCW");
    PCMP   CmpIC  = (PCMP)  GetProcAddress(s, "StrCmpICW");
    PCMPN  CmpNC  = (PCMPN) GetProcAddress(s, "StrCmpNCW");
    PCMPN  CmpNIC = (PCMPN) GetProcAddress(s, "StrCmpNICW");
    PCMPA  CmpCA  = (PCMPA) GetProcAddress(s, "StrCmpCA");
    PCPY   Cpy    = (PCPY)  GetProcAddress(s, "StrCpyW");
    PCPY   Cat    = (PCPY)  GetProcAddress(s, "StrCatW");
    PNCAT  NCat   = (PNCAT) GetProcAddress(s, "StrNCatW");
    PTOI   ToInt  = (PTOI)  GetProcAddress(s, "StrToIntW");
    PTOIA  ToIntA = (PTOIA) GetProcAddress(s, "StrToIntA");
    PTOIEX ToIntEx= (PTOIEX)GetProcAddress(s, "StrToIntExW");
    PTOI64 ToI64  = (PTOI64)GetProcAddress(s, "StrToInt64ExW");
    printf("exports: CmpC %p CmpIC %p CmpNC %p CmpNIC %p CmpCA %p Cpy %p Cat %p NCat %p ToInt %p ToIntA %p ToIntEx %p ToI64 %p\n\n",
        (void*)CmpC,(void*)CmpIC,(void*)CmpNC,(void*)CmpNIC,(void*)CmpCA,(void*)Cpy,(void*)Cat,(void*)NCat,(void*)ToInt,(void*)ToIntA,(void*)ToIntEx,(void*)ToI64);

    SetThreadAffinityMask(GetCurrentThread(), 1u << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    static wchar_t a[4200], b[4200], dst[9000];
    static char aa[4200], bb[4200];
    static const int L[] = { 16, 256, 4096 };
    for (int k = 0; k < 3; ++k) {
        int n = L[k];
        for (int i = 0; i < n; ++i) { a[i] = b[i] = (wchar_t)(L'a' + i % 26); aa[i] = bb[i] = (char)('a' + i % 26); }
        a[n] = b[n] = 0; aa[n] = bb[n] = 0;
        printf("== equal strings / copies of %d characters ==\n", n);
        char lab[64];
        if (CmpC)   { sprintf(lab, "StrCmpCW  %d", n);   TIME(lab, n, sink += CmpC(a, b)); }
        sprintf(lab, "  control wcscmp %d", n);          TIME(lab, n, sink += wcscmp(a, b));
        if (CmpIC)  { sprintf(lab, "StrCmpICW %d", n);   TIME(lab, n, sink += CmpIC(a, b)); }
        sprintf(lab, "  control _wcsicmp %d", n);        TIME(lab, n, sink += _wcsicmp(a, b));
        if (CmpNC)  { sprintf(lab, "StrCmpNCW %d", n);   TIME(lab, n, sink += CmpNC(a, b, n)); }
        if (CmpNIC) { sprintf(lab, "StrCmpNICW %d", n);  TIME(lab, n, sink += CmpNIC(a, b, n)); }
        if (CmpCA)  { sprintf(lab, "StrCmpCA  %d", n);   TIME(lab, n, sink += CmpCA(aa, bb)); }
        sprintf(lab, "  control strcmp %d", n);          TIME(lab, n, sink += strcmp(aa, bb));
        if (Cpy)    { sprintf(lab, "StrCpyW   %d", n);   TIME(lab, n, sink += (uintptr_t)Cpy(dst, a)); }
        sprintf(lab, "  control wcscpy %d", n);          TIME(lab, n, sink += (uintptr_t)wcscpy(dst, a));
        if (Cat)    { sprintf(lab, "StrCatW   %d+%d", n, n); TIME(lab, 2 * n, { dst[n] = 0; memcpy(dst, a, n * 2); sink += (uintptr_t)Cat(dst, b); }); }
        sprintf(lab, "  control wcscat %d+%d", n, n);    TIME(lab, 2 * n, { dst[n] = 0; memcpy(dst, a, n * 2); sink += (uintptr_t)wcscat(dst, b); });
        if (NCat)   { sprintf(lab, "StrNCatW  %d+%d", n, n); TIME(lab, 2 * n, { dst[n] = 0; memcpy(dst, a, n * 2); sink += (uintptr_t)NCat(dst, b, n + 1); }); }
        printf("\n");
    }
    printf("== parsers ==\n");
    if (ToInt)   TIME("StrToIntW \"1234567\"",       7, sink += ToInt(L"1234567"));
    TIME("  control _wtoi \"1234567\"",          7, sink += _wtoi(L"1234567"));
    if (ToIntA)  TIME("StrToIntA \"1234567\"",       7, sink += ToIntA("1234567"));
    if (ToIntEx) { int v; TIME("StrToIntExW \"0x1F2E3D\" hex", 8, { ToIntEx(L"0x1F2E3D", 1, &v); sink += v; }); }
    if (ToI64)   { LONGLONG v; TIME("StrToInt64ExW 19 digits", 19, { ToI64(L"1234567890123456789", 0, &v); sink += (uint64_t)v; }); }
    printf("sink=%llu\n", (unsigned long long)sink);
    return 0;
}
