/* discovery/ntdll_ntcopy_wildcard.c
   Two more ntdll candidates with genuine per-byte work, timed before either becomes a target.

     RtlCopyMemoryNonTemporal(dst, src, n)
         a memcpy variant that is supposed to use streaming stores. Timed against RtlCopyMemory on
         the same buffers, which is the control: the two do identical work and differ only in store
         type, so any gap is implementation. Non-temporal stores lose badly below the cache size and
         win above it, so the interesting question is whether ntdll switches or always streams.

     RtlIsNameInUnUpcasedExpression(Expression, Name, IgnoreCase, UpcaseTable)
         the wildcard matcher behind file name matching. With IgnoreCase = FALSE it touches no case
         table at all, so unlike most of the name-matching surface it is locale-free and therefore
         reproducible. Timed on the patterns that actually occur: a literal, a leading star, a
         trailing star, and the pathological one where a star is followed by more pattern, because
         that is where a backtracking matcher goes quadratic and a good one does not.

   WHAT THIS MEASURES. ns per call and bytes per nanosecond at four lengths, the same diagnostic
   used throughout discovery/: ~4-5 B/ns is a byte loop, ~22 is 16-byte SSE2, ~85 is 32-byte AVX2.
   A cost that is flat in length is a call rather than a loop and cannot be improved.

   NOTHING HERE IS A CONTRACT. It is a shortlist.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;

typedef VOID    (NTAPI *PFN_CPY)(void*, const void*, SIZE_T);
typedef BOOLEAN (NTAPI *PFN_WILD)(const USTR*, const USTR*, BOOLEAN, PWCH);

static PFN_CPY  pNT, pCM;
static PFN_WILD pWild;

static double freq_(void) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)f.QuadPart; }
static volatile uint64_t sink;

#define TIME(NAME, BYTES, BODY)                                                                  \
    do {                                                                                          \
        double fq = freq_(); int inner = 64; double best = 1e300;                                 \
        for (int w = 0; w < 32; ++w) { BODY; }                                                     \
        for (;;) { LARGE_INTEGER a,b; QueryPerformanceCounter(&a);                                 \
            for (int i = 0; i < inner; ++i) { BODY; }                                              \
            QueryPerformanceCounter(&b);                                                           \
            double ns = (double)(b.QuadPart-a.QuadPart)*1e9/fq;                                    \
            if (ns >= 300000.0 || inner >= (1<<24)) break; inner *= 4; }                           \
        for (int t = 0; t < 200; ++t) { LARGE_INTEGER a,b; QueryPerformanceCounter(&a);            \
            for (int i = 0; i < inner; ++i) { BODY; }                                              \
            QueryPerformanceCounter(&b);                                                           \
            double ns = (double)(b.QuadPart-a.QuadPart)*1e9/fq/(double)inner;                      \
            if (ns < best) best = ns; }                                                            \
        printf("  %-34s %9d B %11.2f ns %9.2f B/ns\n", NAME, (int)(BYTES), best,                   \
               (BYTES) > 0 ? (double)(BYTES)/best : 0.0);                                          \
    } while (0)

static void mkustr(USTR* u, wchar_t* b, const wchar_t* s) {
    size_t n = wcslen(s);
    memcpy(b, s, (n + 1) * sizeof(wchar_t));
    u->Length = (USHORT)(n * 2); u->MaximumLength = (USHORT)((n + 1) * 2); u->Buffer = b;
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE nt = LoadLibraryW(L"ntdll.dll");
    pNT   = (PFN_CPY) GetProcAddress(nt, "RtlCopyMemoryNonTemporal");
    pCM   = (PFN_CPY) GetProcAddress(nt, "RtlCopyMemory");
    pWild = (PFN_WILD)GetProcAddress(nt, "RtlIsNameInUnUpcasedExpression");
    printf("exports: NonTemporal=%p CopyMemory=%p Wildcard=%p\n\n",
           (void*)pNT, (void*)pCM, (void*)pWild);

    SetThreadAffinityMask(GetCurrentThread(), 1u << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    if (pNT && pCM) {
        printf("== RtlCopyMemoryNonTemporal vs RtlCopyMemory (identical work, different store type) ==\n");
        static const size_t LEN[] = { 64, 1024, 32768, 262144, 4194304 };
        for (int k = 0; k < 5; ++k) {
            size_t n = LEN[k];
            unsigned char* s = (unsigned char*)VirtualAlloc(NULL, n + 4096, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
            unsigned char* d = (unsigned char*)VirtualAlloc(NULL, n + 4096, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
            memset(s, 0x5A, n); memset(d, 0, n);
            char n1[64], n2[64];
            sprintf(n1, "RtlCopyMemoryNonTemporal %zu", n);
            sprintf(n2, "RtlCopyMemory            %zu", n);
            TIME(n1, n, pNT(d, s, n));
            TIME(n2, n, pCM(d, s, n));
            sink ^= d[0];
            VirtualFree(s, 0, MEM_RELEASE); VirtualFree(d, 0, MEM_RELEASE);
        }
        printf("\n");
    }

    if (pWild) {
        printf("== RtlIsNameInUnUpcasedExpression, IgnoreCase = FALSE (no case table touched) ==\n");
        static wchar_t eb[4096], nb[4096];
        static const wchar_t* PAT[] = { L"*", L"*.txt", L"file*", L"*abc*def*", L"a*b*c*d*e*f*g*h" };
        static const char*    PN[]  = { "\"*\"", "\"*.txt\"", "\"file*\"", "\"*abc*def*\"", "\"a*b*...*h\"" };
        static const int NL[] = { 16, 64, 256, 1024 };
        for (int p = 0; p < 5; ++p) {
            for (int k = 0; k < 4; ++k) {
                int n = NL[k];
                for (int i = 0; i < n; ++i) nb[i] = (wchar_t)(L'a' + (i % 26));
                nb[n] = 0;
                USTR e, nm;
                mkustr(&e, eb, PAT[p]);
                nm.Length = (USHORT)(n * 2); nm.MaximumLength = (USHORT)((n + 1) * 2); nm.Buffer = nb;
                char nmz[80]; sprintf(nmz, "wildcard %-12s name=%d", PN[p], n);
                TIME(nmz, n * 2, sink ^= (uint64_t)pWild(&e, &nm, FALSE, NULL));
            }
        }
        printf("\n");
    }

    printf("sink=%llu\n", (unsigned long long)sink);
    return 0;
}
