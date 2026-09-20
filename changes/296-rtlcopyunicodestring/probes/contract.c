/* changes/296-rtlcopyunicodestring/probes/contract.c
 *
 * THROWAWAY PROBE. Nothing here is a gate; this file exists to turn "MSDN says" into "this machine
 * does". Every answer it prints was then written into reference.c, and correctness.c re-proves all
 * of them against the live export on every build.
 *
 * VOID NTAPI RtlCopyUnicodeString(UNICODE_STRING* dst, const UNICODE_STRING* src)
 *
 * The function returns VOID, so every observable effect is in *dst and in dst->Buffer. Each case
 * below therefore fills the destination buffer with a 0x23 sentinel, runs the LIVE export, and
 * prints the whole struct plus the buffer bytes, including bytes past MaximumLength (to see whether
 * it ever writes outside the declared buffer).
 *
 *   BUILD
 *     . .\tools\vsenv.ps1
 *     cl /nologo /O2 changes\296-rtlcopyunicodestring\probes\contract.c /Fe:contract.exe && .\contract.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } US;
typedef void (NTAPI *pfn)(US*, const US*);
static pfn sys;

/* A destination buffer with a sentinel tail, so "did it write past MaximumLength" is visible. */
#define SLOT 64
static unsigned char dbuf[SLOT];

static void show(const char* what, US* d, int nbytes) {
    printf("  %-46s -> Length=%-5u MaximumLength=%-5u Buffer=%s\n",
           what, d->Length, d->MaximumLength, d->Buffer ? "same" : "NULL");
    printf("      bytes:");
    for (int i = 0; i < nbytes; ++i) printf(" %02X", dbuf[i]);
    printf("\n");
}

static void case1(const char* what, const unsigned char* srcbytes, USHORT srclen, USHORT maxlen, int dump) {
    memset(dbuf, 0x23, sizeof dbuf);
    US d; d.Length = 0xAAAA; d.MaximumLength = maxlen; d.Buffer = (wchar_t*)dbuf;
    US s; s.Length = srclen; s.MaximumLength = 0x7777; s.Buffer = (wchar_t*)srcbytes;
    sys(&d, &s);
    show(what, &d, dump);
}

int main(void) {
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    sys = (pfn)GetProcAddress(h, "RtlCopyUnicodeString");
    if (!sys) { printf("no RtlCopyUnicodeString\n"); return 2; }

    static const unsigned char S[32] = {
        0x41,0x00, 0x42,0x00, 0x43,0x00, 0x44,0x00, 0x45,0x00, 0x46,0x00, 0x47,0x00, 0x48,0x00,
        0x49,0x00, 0x4A,0x00, 0x4B,0x00, 0x4C,0x00, 0x4D,0x00, 0x4E,0x00, 0x4F,0x00, 0x50,0x00 };

    printf("== 1. src == NULL ==============================================\n");
    {
        memset(dbuf, 0x23, sizeof dbuf);
        US d; d.Length = 6; d.MaximumLength = 20; d.Buffer = (wchar_t*)dbuf;
        sys(&d, NULL);
        show("src=NULL, dst{L=6,M=20}", &d, 12);
        printf("      => dst->Length zeroed? %s ; buffer untouched? %s ; MaximumLength kept? %s\n",
               d.Length == 0 ? "YES" : "NO",
               dbuf[0] == 0x23 ? "YES" : "NO",
               d.MaximumLength == 20 ? "YES" : "NO");
    }

    printf("\n== 2. room to spare: is a terminating NUL written? =============\n");
    case1("src L=8, max=20", S, 8, 20, 14);
    case1("src L=8, max=10  (exactly Length+2)", S, 8, 10, 14);
    case1("src L=8, max=8   (exact fit, no room for NUL)", S, 8, 8, 14);
    case1("src L=8, max=9   (odd, one spare byte)", S, 8, 9, 14);

    printf("\n== 3. TRUNCATION: src->Length > dst->MaximumLength =============\n");
    case1("src L=20, max=8  (even truncation)", S, 20, 8, 14);
    case1("src L=20, max=7  (ODD MaximumLength)", S, 20, 7, 14);
    case1("src L=20, max=1  (ODD, one byte)", S, 20, 1, 8);
    case1("src L=20, max=3  (ODD, three bytes)", S, 20, 3, 8);
    case1("src L=20, max=0", S, 20, 0, 8);
    printf("      => does truncation round down to a whole WCHAR, or copy MaximumLength exactly?\n");

    printf("\n== 4. ODD src->Length with room to spare =======================\n");
    case1("src L=5, max=40  (odd source length)", S, 5, 40, 12);
    case1("src L=1, max=40", S, 1, 40, 12);
    case1("src L=3, max=40", S, 3, 40, 12);
    printf("      => where does the NUL land when Length is odd (floor(L/2) or ceil)?\n");

    printf("\n== 5. empty source ============================================\n");
    case1("src L=0, max=20", S, 0, 20, 8);
    case1("src L=0, max=0", S, 0, 0, 8);
    case1("src L=0, max=1", S, 0, 1, 8);

    printf("\n== 6. dst->Buffer == NULL with MaximumLength == 0 ==============\n");
    {
        US d; d.Length = 0x1234; d.MaximumLength = 0; d.Buffer = NULL;
        US s; s.Length = 8; s.MaximumLength = 8; s.Buffer = (wchar_t*)S;
        sys(&d, &s);
        printf("  survived. Length=%u MaximumLength=%u Buffer=%p\n", d.Length, d.MaximumLength, (void*)d.Buffer);
    }
    {
        US d; d.Length = 0x1234; d.MaximumLength = 0; d.Buffer = NULL;
        US s; s.Length = 0; s.MaximumLength = 0; s.Buffer = NULL;
        sys(&d, &s);
        printf("  src L=0 too: Length=%u MaximumLength=%u\n", d.Length, d.MaximumLength);
    }

    printf("\n== 7. is src->MaximumLength read at all? =======================\n");
    {
        memset(dbuf, 0x23, sizeof dbuf);
        US d; d.Length = 0; d.MaximumLength = 40; d.Buffer = (wchar_t*)dbuf;
        US s; s.Length = 8; s.MaximumLength = 0; s.Buffer = (wchar_t*)S;  /* MaximumLength lies */
        sys(&d, &s);
        show("src{L=8,M=0}", &d, 14);
        printf("      => copied anyway? %s (src->MaximumLength ignored)\n", d.Length == 8 ? "YES" : "NO");
    }

    printf("\n== 8. OVERLAP: does the shipped copy behave like memmove? ======\n");
    {
        /* dst ahead of src by 4 bytes inside one buffer: a forward byte/block copy would smear. */
        static unsigned char ov[64];
        for (int i = 0; i < 64; ++i) ov[i] = (unsigned char)(0x10 + i);
        US d; d.Length = 0; d.MaximumLength = 40; d.Buffer = (wchar_t*)(ov + 4);
        US s; s.Length = 32; s.MaximumLength = 32; s.Buffer = (wchar_t*)ov;
        sys(&d, &s);
        printf("  dst = src+4, n=32:");
        for (int i = 0; i < 40; ++i) printf(" %02X", ov[i]);
        printf("\n      expected if memmove: 10 11 12 13 10 11 12 13 14 ...\n");
    }
    {
        static unsigned char ov[64];
        for (int i = 0; i < 64; ++i) ov[i] = (unsigned char)(0x10 + i);
        US d; d.Length = 0; d.MaximumLength = 40; d.Buffer = (wchar_t*)ov;
        US s; s.Length = 32; s.MaximumLength = 32; s.Buffer = (wchar_t*)(ov + 4);
        sys(&d, &s);
        printf("  dst = src-4, n=32:");
        for (int i = 0; i < 40; ++i) printf(" %02X", ov[i]);
        printf("\n");
    }
    {
        /* large enough to reach the block loop, dst ahead by 8 */
        static unsigned char ov[1024];
        for (int i = 0; i < 1024; ++i) ov[i] = (unsigned char)(i & 0xFF);
        unsigned char ref[1024];
        memcpy(ref, ov, 1024);
        memmove(ref + 8, ref, 512);
        US d; d.Length = 0; d.MaximumLength = 1000; d.Buffer = (wchar_t*)(ov + 8);
        US s; s.Length = 512; s.MaximumLength = 512; s.Buffer = (wchar_t*)ov;
        sys(&d, &s);
        int bad = -1;
        for (int i = 0; i < 520; ++i) if (ov[i] != ref[i]) { bad = i; break; }
        printf("  dst = src+8, n=512: matches C memmove? %s%s", bad < 0 ? "YES" : "NO", bad < 0 ? "\n" : "");
        if (bad >= 0) printf(" (first diff at %d: got %02X want %02X)\n", bad, ov[bad], ref[bad]);
    }

    printf("\n== 9. does it ever write past MaximumLength? (page guard) ======\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        DWORD ps = si.dwPageSize;
        unsigned char* base = (unsigned char*)VirtualAlloc(NULL, ps * 2, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(base, ps, MEM_COMMIT, PAGE_READWRITE);
        for (USHORT n = 0; n <= 128; ++n) {
            unsigned char* p = base + ps - n;            /* ends exactly at the guard page */
            memset(p, 0x23, n);
            US d; d.Length = 0; d.MaximumLength = n; d.Buffer = (wchar_t*)p;
            US s; s.Length = (USHORT)(n + 16); s.MaximumLength = (USHORT)(n + 16); s.Buffer = (wchar_t*)S;
            if (s.Length > 32) { s.Length = 32; s.MaximumLength = 32; }
            sys(&d, &s);                                  /* faults if it reads/writes the guard */
        }
        printf("  128 destinations ending exactly at a PAGE_NOACCESS boundary: no fault.\n");
        /* and the source side: src ending at the guard */
        for (USHORT n = 2; n <= 128; n += 2) {
            unsigned char* p = base + ps - n;
            memset(p, 0x41, n);
            static unsigned char big[512];
            US d; d.Length = 0; d.MaximumLength = 400; d.Buffer = (wchar_t*)big;
            US s; s.Length = n; s.MaximumLength = n; s.Buffer = (wchar_t*)p;
            sys(&d, &s);
        }
        printf("  64 sources ending exactly at a PAGE_NOACCESS boundary: no fault.\n");
    }

    printf("\n== 10. is dst->MaximumLength ever modified? ====================\n");
    {
        int changed = 0;
        for (int i = 0; i < 400; ++i) {
            memset(dbuf, 0x23, sizeof dbuf);
            USHORT mx = (USHORT)(i % 50);
            US d; d.Length = (USHORT)(i % 7); d.MaximumLength = mx; d.Buffer = (wchar_t*)dbuf;
            US s; s.Length = (USHORT)(i % 37); s.MaximumLength = 0; s.Buffer = (wchar_t*)S;
            if (s.Length > 32) s.Length = 32;
            if (mx == 0 && d.Buffer == NULL) continue;
            sys(&d, &s);
            if (d.MaximumLength != mx) { changed = 1; break; }
        }
        printf("  MaximumLength modified in 400 varied calls? %s\n", changed ? "YES" : "NO");
    }

    printf("\n== 11. does dst->Length come out clamped, or raw? ==============\n");
    {
        memset(dbuf, 0x23, sizeof dbuf);
        US d; d.Length = 0; d.MaximumLength = 7; d.Buffer = (wchar_t*)dbuf;
        US s; s.Length = 9; s.MaximumLength = 9; s.Buffer = (wchar_t*)S;
        sys(&d, &s);
        printf("  src L=9 into max=7 -> dst->Length=%u (7 = raw MaximumLength, 6 = rounded to WCHAR)\n", d.Length);
    }
    return 0;
}
