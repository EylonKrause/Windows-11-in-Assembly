/* discovery/charupperw_string.c
   user32!CharUpperW / CharLowerW in STRING mode, which nothing in this repository has timed.

   WHY. Change 277 converted CharUpperBuffW / CharLowerBuffW at 7.94x after probes/mapping.c showed
   them to be a pure per-code-unit table identical to RtlUpcaseUnicodeChar / RtlDowncaseUnicodeChar,
   not locale-aware even under Turkish. Their siblings CharUpperW / CharLowerW take one LPWSTR that is
   EITHER a character in its low word OR a pointer to a NUL-terminated string mapped in place, and
   discovery/rtl_integer_char.c only ever timed the character form. If the string form is the same
   table walked one character at a time behind a length scan, 277's machinery applies directly.

   WHAT THIS ESTABLISHES, each a thing a reimplementation would otherwise have to guess:
     1. how a 64-bit argument is classified -- a full IS_INTRESOURCE test (value >> 16 == 0), or only
        the high word of the LOW dword, which would treat a string at 0x1'0000'xxxx as a character;
     2. the return value in both modes;
     3. whether an unreadable string pointer faults or is swallowed by an exception handler;
     4. string mode against RtlUpcaseUnicodeChar / RtlDowncaseUnicodeChar on all 65535 non-NUL units;
     5. the cost per character in string mode, against CharUpperBuffW on the same text.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef LPWSTR (WINAPI *PFN_CU)(LPWSTR);
typedef DWORD  (WINAPI *PFN_CUB)(LPWSTR, DWORD);
typedef WCHAR  (NTAPI *PFN_RC)(WCHAR);
static PFN_CU  pUp, pLo;
static PFN_CUB pUpB;
static PFN_RC  pRUp, pRDn;

static double freq_(void) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)f.QuadPart; }
static volatile uint64_t sink;

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE u = LoadLibraryW(L"user32.dll"), n = GetModuleHandleW(L"ntdll.dll");
    pUp  = (PFN_CU) GetProcAddress(u, "CharUpperW");
    pLo  = (PFN_CU) GetProcAddress(u, "CharLowerW");
    pUpB = (PFN_CUB)GetProcAddress(u, "CharUpperBuffW");
    pRUp = (PFN_RC) GetProcAddress(n, "RtlUpcaseUnicodeChar");
    pRDn = (PFN_RC) GetProcAddress(n, "RtlDowncaseUnicodeChar");

    printf("== 1/2. classification and return value ==\n");
    printf("  CharUpperW(0x0061)  -> %p\n", (void*)pUp((LPWSTR)(UINT_PTR)0x0061));
    printf("  CharUpperW(0xFFFF)  -> %p\n", (void*)pUp((LPWSTR)(UINT_PTR)0xFFFF));
    printf("  CharLowerW(0x0041)  -> %p\n", (void*)pLo((LPWSTR)(UINT_PTR)0x0041));
    {
        /* a real string at an address whose LOW dword has a zero high word */
        static const UINT_PTR TRY[] = { 0x100000000ull, 0x200000000ull, 0x7FF000000000ull };
        for (int i = 0; i < 3; ++i) {
            wchar_t* s = (wchar_t*)VirtualAlloc((void*)TRY[i], 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (!s) { printf("  (could not place a string at %llx)\n", (unsigned long long)TRY[i]); continue; }
            wcscpy(s + 0x30, L"abc");                        /* low dword 0x00000060 */
            wcscpy(s, L"xyz");                               /* low dword 0x00000000 */
            LPWSTR r1 = pUp(s + 0x30), r0 = pUp(s);
            printf("  string at %p: returned %p, now \"%ls\"   |   string at %p: returned %p, now \"%ls\"\n",
                   (void*)(s + 0x30), (void*)r1, s + 0x30, (void*)s, (void*)r0, s);
            VirtualFree(s, 0, MEM_RELEASE);
        }
    }

    printf("\n== 3. an unreadable string pointer ==\n");
    {
        wchar_t* bad = (wchar_t*)VirtualAlloc(NULL, 0x1000, MEM_RESERVE, PAGE_NOACCESS);
        int faulted = 0; LPWSTR r = 0;
        __try { r = pUp(bad); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
        printf("  CharUpperW(NOACCESS) -> %s (returned %p)\n", faulted ? "FAULTED" : "returned normally", (void*)r);
        /* a string that runs INTO a NOACCESS page with no terminator */
        unsigned char* b = (unsigned char*)VirtualAlloc(NULL, 0x2000, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(b, 0x1000, MEM_COMMIT, PAGE_READWRITE);
        wchar_t* t = (wchar_t*)(b + 0x1000) - 4;
        t[0] = L'a'; t[1] = L'b'; t[2] = L'c'; t[3] = L'd';
        faulted = 0; r = 0;
        __try { r = pUp(t); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
        printf("  unterminated into NOACCESS -> %s; buffer now %.4ls (returned %p)\n",
               faulted ? "FAULTED" : "returned normally", t, (void*)r);
    }

    printf("\n== 4. string mode vs the ntdll char mapping, every non-NUL code unit ==\n");
    {
        static wchar_t buf[65536];
        for (int i = 1; i < 65536; ++i) buf[i - 1] = (wchar_t)i;
        buf[65535] = 0;
        pUp(buf);
        int du = 0; for (int i = 1; i < 65536; ++i) if (buf[i - 1] != pRUp((WCHAR)i)) ++du;
        for (int i = 1; i < 65536; ++i) buf[i - 1] = (wchar_t)i;
        pLo(buf);
        int dl = 0; for (int i = 1; i < 65536; ++i) if (buf[i - 1] != pRDn((WCHAR)i)) ++dl;
        printf("  CharUpperW string mode vs RtlUpcaseUnicodeChar:   %d of 65535 differ\n", du);
        printf("  CharLowerW string mode vs RtlDowncaseUnicodeChar: %d of 65535 differ\n", dl);
        /* character mode too */
        int cu = 0, cl = 0;
        for (int i = 1; i < 65536; ++i) {
            if ((WCHAR)(UINT_PTR)pUp((LPWSTR)(UINT_PTR)i) != pRUp((WCHAR)i)) ++cu;
            if ((WCHAR)(UINT_PTR)pLo((LPWSTR)(UINT_PTR)i) != pRDn((WCHAR)i)) ++cl;
        }
        printf("  character mode: upper %d, lower %d differ\n", cu, cl);
    }

    printf("\n== 5. cost per character, string mode vs CharUpperBuffW (ASCII text) ==\n");
    SetThreadAffinityMask(GetCurrentThread(), 1u << 2);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    static wchar_t src[4100], work[4100];
    for (int i = 0; i < 4096; ++i) src[i] = (wchar_t)(L'a' + i % 26);
    static const int L[] = { 8, 64, 256, 1024, 4096 };
    for (int k = 0; k < 5; ++k) {
        int len = L[k];
        double fq = freq_(), b1 = 1e300, b2 = 1e300;
        for (int t = 0; t < 400; ++t) {
            LARGE_INTEGER a, b;
            memcpy(work, src, len * 2); work[len] = 0;
            QueryPerformanceCounter(&a); sink += (uintptr_t)pUp(work); QueryPerformanceCounter(&b);
            double ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / fq; if (ns < b1) b1 = ns;
            memcpy(work, src, len * 2); work[len] = 0;
            QueryPerformanceCounter(&a); sink += pUpB(work, (DWORD)len); QueryPerformanceCounter(&b);
            ns = (double)(b.QuadPart - a.QuadPart) * 1e9 / fq; if (ns < b2) b2 = ns;
        }
        printf("  %5d chars   CharUpperW %9.1f ns (%.3f ns/char)   CharUpperBuffW %9.1f ns (%.3f ns/char)\n",
               len, b1, b1 / len, b2, b2 / len);
    }
    return 0;
}
