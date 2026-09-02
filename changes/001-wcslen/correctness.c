// changes/001-wcslen/correctness.c
// Gate 1: prove wia_wcslen is bit-exact vs the scalar reference AND behavior-
// identical to the live system wcslen (loaded from ucrtbase.dll on THIS pc),
// including unaligned starts and a hard page-boundary over-read test.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>

extern size_t wia_wcslen(const wchar_t*);
size_t ref_wcslen(const wchar_t*);

typedef size_t (__cdecl *wcslen_fn)(const wchar_t*);

static int failures = 0;
static void check(const wchar_t* s, size_t ref, size_t ours, size_t sys, const char* what) {
    if (ours != ref || sys != ref) {
        printf("FAIL [%s]: ref=%zu ours=%zu sys=%zu\n", what, ref, ours, sys);
        ++failures;
    }
}

int main(void) {
    HMODULE h = LoadLibraryW(L"ucrtbase.dll");
    if (!h) { printf("cannot load ucrtbase.dll\n"); return 2; }
    wcslen_fn sys_wcslen = (wcslen_fn)GetProcAddress(h, "wcslen");
    if (!sys_wcslen) { printf("no wcslen export in ucrtbase\n"); return 2; }

    // 1) fuzz: every length 0..300, every start offset 0..15 wchars.
    static wchar_t buf[512];
    unsigned long seed = 0x1234567u;
    for (size_t len = 0; len <= 300; ++len) {
        for (int off = 0; off < 16; ++off) {
            wchar_t* s = buf + off;
            for (size_t i = 0; i < len; ++i) {
                seed = seed * 1103515245u + 12345u;      // never produce a 0
                wchar_t c = (wchar_t)((seed >> 16) | 1);
                s[i] = c ? c : 1;
            }
            s[len] = 0;
            size_t r = ref_wcslen(s), o = wia_wcslen(s), y = sys_wcslen(s);
            check(s, r, o, y, "fuzz");
        }
    }

    // 2) page-boundary over-read test. Two pages; second is PAGE_NOACCESS.
    //    Place the string so its terminator sits at the very last wchar slot of
    //    page 1: any read into page 2 faults. Slide the terminator across the
    //    last 40 wchars of page 1 to exercise the tail of both the prologue and
    //    the aligned loop right against the guard.
    SYSTEM_INFO si; GetSystemInfo(&si);
    DWORD pg = si.dwPageSize;                       // 4096
    unsigned char* base = (unsigned char*)VirtualAlloc(NULL, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!base) { printf("VirtualAlloc failed\n"); return 2; }
    DWORD old;
    VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);
    for (int tail = 2; tail <= 80; tail += 2) {     // bytes of string+term before the guard
        unsigned char* end = base + pg;             // first NOACCESS byte
        wchar_t* term = (wchar_t*)(end - tail);     // where the 0 terminator goes
        // fill from term backward-ish: put non-zero wchars up to term, 0 at term
        wchar_t* start = (wchar_t*)(base + pg - 256);// well inside page 1
        for (wchar_t* p = start; p < term; ++p) *p = L'A';
        *term = 0;
        size_t r = ref_wcslen(start), o = wia_wcslen(start), y = sys_wcslen(start);
        check(start, r, o, y, "pageguard");
        // also from an unaligned start near the terminator
        wchar_t* start2 = term - 3;
        if ((unsigned char*)start2 >= base) {
            *start2 = L'X'; *(start2+1)=L'Y'; *(start2+2)=L'Z';
            size_t r2 = ref_wcslen(start2), o2 = wia_wcslen(start2), y2 = sys_wcslen(start2);
            check(start2, r2, o2, y2, "pageguard-near");
        }
    }

    if (failures == 0) printf("CORRECTNESS: PASS (fuzz 0..300 x 16 offsets + page-guard tail)\n");
    else               printf("CORRECTNESS: FAIL (%d mismatches)\n", failures);
    return failures ? 1 : 0;
}
