// changes/299-sysallocstring/correctness.c
//
// Gate 1: wia_sysallocstring vs the live oleaut32!SysAllocString and an independent oracle.
//
// What is compared. a BSTR is not just a pointer: it carries a byte-count prefix four bytes in
// front of it, a terminator the length does not include, and an identity as a block the caller
// will hand to SysFreeString. So every case compares
//
//     NULL-ness      -- a NULL BSTR and a zero-length BSTR both answer 0 to SysStringLen, so the
//                       empty-string corner is invisible to any check that only asks for a length
//     the PREFIX at [-4]  -- the byte count, read directly rather than through SysStringByteLen
//     SysStringLen / SysStringByteLen
//     every byte of the string AND its terminator
//
// and then frees all three blocks, because a gate that leaks measures the allocator degrading.
//
// The NOACCESS sweep is the point of the aligned load. The implementation aligns its first load
// DOWN to a 32-byte boundary, which is safe only because a 32-byte aligned load cannot cross a page
// boundary. The sweep puts a string so that its terminator is the last thing in a committed page
// with a NOACCESS page immediately after, at every length and therefore at every alignment, so any
// read past the terminator faults rather than passing quietly.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

extern BSTR wia_sysallocstring(const wchar_t*);
BSTR ref_sysallocstring(const wchar_t*);

typedef BSTR (WINAPI *fn)(const OLECHAR*);
static fn sys;
static int failures = 0;

static unsigned prefix_of(BSTR b) { return ((unsigned*)b)[-1]; }

static void chk(const wchar_t* s, const char* what)
{
    BSTR a, b, r;
    int bad = 0;
    if (failures > 8) return;
    a = sys(s);
    b = wia_sysallocstring(s);
    r = ref_sysallocstring(s);

    if ((a == NULL) != (b == NULL) || (a == NULL) != (r == NULL)) bad = 1;
    if (!bad && a) {
        if (SysStringLen(a) != SysStringLen(b) || SysStringLen(a) != SysStringLen(r)) bad = 1;
        if (SysStringByteLen(a) != SysStringByteLen(b) ||
            SysStringByteLen(a) != SysStringByteLen(r)) bad = 1;
        if (prefix_of(a) != prefix_of(b) || prefix_of(a) != prefix_of(r)) bad = 1;
        if (!bad) {
            size_t n = (size_t)SysStringByteLen(a) + 2;     /* + the terminator */
            if (memcmp(a, b, n) != 0 || memcmp(a, r, n) != 0) bad = 1;
        }
    }
    if (bad) {
        printf("FAIL %s: sys=%p(len %u) ours=%p(len %u) ref=%p(len %u)\n",
               what, (void*)a, a ? SysStringLen(a) : 0,
               (void*)b, b ? SysStringLen(b) : 0,
               (void*)r, r ? SysStringLen(r) : 0);
        ++failures;
    }
    SysFreeString(a); SysFreeString(b); SysFreeString(r);
}

int main(void)
{
    static wchar_t buf[4200];
    static wchar_t emb[32];
    int len, align, i;
    long checks = 0;

    { HMODULE h = LoadLibraryW(L"oleaut32.dll"); sys = (fn)GetProcAddress(h, "SysAllocString"); }
    if (!sys) { printf("no SysAllocString\n"); return 2; }

    /* ---- the named corners ------------------------------------------------------------- */
    chk(NULL,        "NULL");                                  ++checks;
    chk(L"",         "empty string -> a REAL zero-length BSTR"); ++checks;
    chk(L"a",        "one character");                         ++checks;
    chk(L"abcdefgh", "eight characters");                      ++checks;

    /* an embedded NUL: the measurement must stop at the first one and ignore what follows */
    emb[0]=L'a'; emb[1]=L'b'; emb[2]=0; emb[3]=L'c'; emb[4]=L'd'; emb[5]=0;
    chk(emb, "embedded NUL at index 2");                       ++checks;
    emb[0]=0; emb[1]=L'x'; emb[2]=L'y'; emb[3]=0;
    chk(emb, "NUL at index 0 with data after it");             ++checks;

    /* ---- every length x every 32-byte alignment ----------------------------------------- */
    for (align = 0; align < 16 && failures <= 8; ++align) {
        wchar_t* p = buf + align;
        for (len = 0; len <= 300 && failures <= 8; ++len) {
            for (i = 0; i < len; ++i) p[i] = (wchar_t)(L'a' + (i % 26));
            p[len] = 0;
            /* non-ASCII too: the scan is byte-blind, but a table-driven mistake would not be */
            if ((len % 7) == 0)
                for (i = 0; i < len; i += 3) p[i] = (wchar_t)(0x0400 + (i % 0x400));
            chk(p, "length x alignment");
            ++checks;
        }
    }

    /* ---- long strings, where the scan is many blocks ------------------------------------ */
    for (len = 1000; len <= 4096 && failures <= 8; len += 1031) {
        for (i = 0; i < len; ++i) buf[i] = (wchar_t)(L'a' + (i % 26));
        buf[len] = 0;
        chk(buf, "long");
        ++checks;
    }

    /* ---- NOACCESS page guard: the terminator is the last thing in a committed page ------- */
    {
        SYSTEM_INFO si;
        char* mem;
        GetSystemInfo(&si);
        mem = (char*)VirtualAlloc(0, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (!mem) { printf("guard reserve failed\n"); return 2; }
        if (!VirtualAlloc(mem, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE)) {
            printf("guard commit failed\n"); return 2; }
        for (len = 0; len < 400 && failures <= 8; ++len) {
            wchar_t* p = (wchar_t*)(mem + si.dwPageSize - (size_t)(len + 1) * 2);
            for (i = 0; i < len; ++i) p[i] = (wchar_t)(L'a' + (i % 26));
            p[len] = 0;
            chk(p, "page guard");
            ++checks;
        }
    }

    if (!failures)
        printf("CORRECTNESS: PASS (SysAllocString vs live oleaut32 + oracle, %ld checks: NULL, the\n"
               "  empty string compared BY POINTER so a zero-length BSTR is distinguished from a NULL\n"
               "  one, embedded NULs including one at index 0, lengths 0..300 x 16 alignments with\n"
               "  non-ASCII every seventh, strings of 1000..4096 characters, and a NOACCESS page-guard\n"
               "  sweep at every length -- comparing the [-4] byte prefix, both lengths, and every\n"
               "  byte including the terminator)\n", checks);
    else
        printf("CORRECTNESS: FAIL (%d)\n", failures);
    return failures ? 1 : 0;
}
