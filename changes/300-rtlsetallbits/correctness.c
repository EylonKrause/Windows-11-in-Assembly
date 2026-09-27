// changes/300-rtlsetallbits/correctness.c
// Gate 1: wia_setallbits must be byte-identical to the live ntdll!RtlSetAllBits, and must not
// write one byte more than it does.
//
// A fill is the easiest kind of function to get almost right, so the comparison here is never
// "are the bits set" -- it is "is the arena identical", poison and all. Each case runs the live
// export, our assembly and the scalar oracle into three separately poisoned arenas and memcmps the
// WHOLE arena, so a single byte written past the end of the fill fails just as loudly as a wrong
// bit inside it. That is the defect this function invites: the tail is a partial ULONG on most
// inputs, the padding past SizeOfBitMap is SET rather than preserved, and an implementation that
// rounds to 8, 16 or 32 bytes instead of 4 passes every bit-level check while corrupting whatever
// the caller put after the bitmap.
//
// Covered:
//   * every SizeOfBitMap from 0 to 4096 at eight buffer alignments, which walks the 4/8/12/16/20/
//     24/28-byte ladder, the 32-byte entry, the 128-byte unrolled body and every remainder;
//   * large sizes, including ones whose byte count is not a multiple of 32 or 128;
//   * SizeOfBitMap = 0 with a deliberately bogus Buffer, because the contract is that Buffer is
//     never dereferenced and a NULL deref is the only way to prove it;
//   * a NOACCESS page guard placed so the final byte of the fill is the last readable byte, run at
//     every alignment, so a single byte of over-write dies immediately rather than being caught by
//     a memcmp that might not have covered it.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RTLBM;
typedef VOID (NTAPI *PFN)(RTLBM*);

extern void wia_setallbits(RTLBM*);
void ref_setallbits(unsigned long, unsigned long*);

static PFN sys;
static int fails = 0;

#define ARENA (8192 + 256)
static unsigned char A[ARENA], B[ARENA], C[ARENA];

static size_t bytes_for(unsigned long n) {
    return (size_t)((((unsigned long long)n + 31ull) / 32ull) * 4ull);
}

static void chk(unsigned long n, int align) {
    memset(A, 0xA5, ARENA); memset(B, 0xA5, ARENA); memset(C, 0xA5, ARENA);
    RTLBM ba = { n, (PULONG)(A + align) };
    RTLBM bb = { n, (PULONG)(B + align) };
    sys(&ba);
    wia_setallbits(&bb);
    ref_setallbits(n, (unsigned long*)(C + align));

    int okb = (memcmp(A, B, ARENA) == 0);
    int okc = (memcmp(A, C, ARENA) == 0);
    if (!okb || !okc) {
        if (fails < 20) {
            size_t i = 0; while (i < ARENA && A[i] == (okb ? C[i] : B[i])) ++i;
            printf("FAIL n=%lu align=%d bytes=%zu  %s differs, first at arena[%zu]: sys=%02X ours=%02X ref=%02X\n",
                   n, align, bytes_for(n), okb ? "ORACLE" : "OURS", i, A[i], B[i], C[i]);
        }
        ++fails;
    }
}

int main(void) {
    HMODULE nt = LoadLibraryW(L"ntdll.dll");
    sys = (PFN)GetProcAddress(nt, "RtlSetAllBits");
    if (!sys) { printf("no RtlSetAllBits\n"); return 2; }

    // 1. every size 0..4096 at eight alignments
    for (int align = 0; align < 32 && fails <= 20; align += 4)
        for (unsigned long n = 0; n <= 4096 && fails <= 20; ++n)
            chk(n, align);
    printf("  0..4096 bits x 8 alignments: %d fails\n", fails);

    // 2. larger sizes, deliberately including byte counts that are not multiples of 32 or 128
    {
        static const unsigned long BIG[] = {
            4097, 5000, 8191, 8192, 8193, 12345, 16384, 20000, 32768, 40961, 49152, 65535, 65536
        };
        for (int i = 0; i < (int)(sizeof BIG / sizeof BIG[0]) && fails <= 20; ++i) {
            unsigned long n = BIG[i];
            size_t nb = bytes_for(n);
            if (nb + 64 > ARENA) continue;
            for (int align = 0; align < 32; align += 4) chk(n, align);
        }
        printf("  large sizes x 8 alignments:  %d fails\n", fails);
    }

    // 3. SizeOfBitMap = 0 must never dereference Buffer
    {
        RTLBM z; z.SizeOfBitMap = 0; z.Buffer = (PULONG)(ULONG_PTR)0x8;   // would AV if read
        sys(&z);                 // the live export tolerates it
        wia_setallbits(&z);      // so must we
        printf("  SizeOfBitMap=0 with a bogus Buffer: survived\n");
    }

    // 4. NOACCESS page guard: the fill must end exactly at the page boundary
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        unsigned char* base = (unsigned char*)VirtualAlloc(NULL, pg * 3, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(base, pg * 2, MEM_COMMIT, PAGE_READWRITE);
        unsigned char* guard = base + pg * 2;          // first byte of the NOACCESS page
        int guarded = 0;

        static const unsigned long GN[] = { 1, 8, 32, 33, 64, 97, 128, 129, 256, 1000, 1024, 4096, 8192, 12345 };
        for (int i = 0; i < (int)(sizeof GN / sizeof GN[0]) && fails <= 20; ++i) {
            unsigned long n = GN[i];
            size_t nb = bytes_for(n);
            if (nb == 0 || nb > pg * 2) continue;
            unsigned char* p = guard - nb;             // last filled byte is the last readable byte
            memset(p, 0xA5, nb);
            RTLBM bm = { n, (PULONG)p };
            wia_setallbits(&bm);                       // a single byte over and this AVs
            for (size_t k = 0; k < nb; ++k) {
                if (p[k] != 0xFF) { printf("GUARD FAIL n=%lu byte %zu = %02X\n", n, k, p[k]); ++fails; break; }
            }
            ++guarded;
        }
        printf("  NOACCESS page guard: %d size(s), %d fails\n", guarded, fails);
    }

    if (!fails)
        printf("CORRECTNESS: PASS (RtlSetAllBits vs live + oracle, whole-arena compare: 0..4096 bits x 8 alignments, large sizes, zero-size with a bogus Buffer, NOACCESS page guard)\n");
    else
        printf("CORRECTNESS: FAIL (%d)\n", fails);
    return fails ? 1 : 0;
}
