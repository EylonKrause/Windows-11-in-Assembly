// changes/002-memchr/correctness.c
// Gate 1: wia_memchr bit-exact vs scalar reference AND vs live ucrtbase memchr,
// including a bounded page-guard test that would fault on any over-read past p+n.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

extern void* wia_memchr(const void*, int, size_t);
void* ref_memchr(const void*, int, size_t);
typedef void* (__cdecl *memchr_fn)(const void*, int, size_t);

static int failures = 0;
static void check(void* r, void* o, void* y, const char* what, size_t n, int off, int pos) {
    if (o != r || y != r) {
        printf("FAIL [%s] n=%zu off=%d pos=%d: ref=%p ours=%p sys=%p\n",
               what, n, off, pos, r, o, y);
        ++failures;
    }
}

int main(void) {
    HMODULE h = LoadLibraryW(L"ucrtbase.dll");
    memchr_fn sys_memchr = (memchr_fn)GetProcAddress(h, "memchr");
    if (!sys_memchr) { printf("no memchr export\n"); return 2; }

    static unsigned char buf[512];
    // 1) fuzz: length 0..300, start offset 0..31, target present at every
    //    position and absent.
    for (size_t n = 0; n <= 300; ++n) {
        for (int off = 0; off < 32; ++off) {
            unsigned char* s = buf + off;
            for (size_t i = 0; i < n; ++i) s[i] = 0xAA;
            // absent
            void* r = ref_memchr(s, 0x55, n);
            void* o = wia_memchr(s, 0x55, n);
            void* y = sys_memchr(s, 0x55, n);
            check(r, o, y, "absent", n, off, -1);
            // present at each position (a few, to keep it quick for large n)
            for (size_t pos = 0; pos < n; pos += (n > 40 ? 7 : 1)) {
                unsigned char save = s[pos];
                s[pos] = 0x55;
                r = ref_memchr(s, 0x55, n);
                o = wia_memchr(s, 0x55, n);
                y = sys_memchr(s, 0x55, n);
                check(r, o, y, "present", n, off, (int)pos);
                s[pos] = save;
            }
        }
    }

    // 2) bounded page-guard: buffer ends exactly at a NOACCESS page, target
    //    absent, so any read past p+n faults. Slide n across the last 40 bytes.
    SYSTEM_INFO si; GetSystemInfo(&si);
    DWORD pg = si.dwPageSize;
    unsigned char* base = (unsigned char*)VirtualAlloc(NULL, pg * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD old; VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);
    for (size_t n = 1; n <= 40; ++n) {
        unsigned char* s = base + pg - n;           // ends at the guard
        for (size_t i = 0; i < n; ++i) s[i] = 0xAA;
        void* r = ref_memchr(s, 0x55, n);           // absent -> full scan to p+n
        void* o = wia_memchr(s, 0x55, n);
        void* y = sys_memchr(s, 0x55, n);
        check(r, o, y, "pageguard-absent", n, 0, -1);
        // present at last byte
        s[n-1] = 0x55;
        r = ref_memchr(s, 0x55, n); o = wia_memchr(s, 0x55, n); y = sys_memchr(s, 0x55, n);
        check(r, o, y, "pageguard-last", n, 0, (int)(n-1));
        s[n-1] = 0xAA;
    }

    if (failures == 0) printf("CORRECTNESS: PASS (fuzz 0..300 x 32 offsets, present/absent + page-guard)\n");
    else               printf("CORRECTNESS: FAIL (%d mismatches)\n", failures);
    return failures ? 1 : 0;
}
