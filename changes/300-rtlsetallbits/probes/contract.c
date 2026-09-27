/* changes/300-rtlsetallbits/probes/contract.c
   Exactly how much of Buffer does RtlSetAllBits write, and what does it leave in the bits past
   SizeOfBitMap?

   This is the whole risk in the change. The timing says the small-bitmap path is worth taking, but
   a fill is only reproducible if the number of bytes touched and the state of the trailing padding
   are both pinned down. Three candidate rules disagree only in the last word:

     (a) exactly ceil(N/8) bytes, padding bits inside the final byte set to 1
     (b) exactly ceil(N/8) bytes, padding bits inside the final byte preserved
     (c) whole ULONGs -- ceil(N/32)*4 bytes -- padding set to 1 out to the word boundary

   The buffer is poisoned with 0xA5 and framed by poison on both sides, so the first and last
   modified byte are both read straight off the result rather than assumed.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RTLBM;
typedef VOID (NTAPI *PFN_INIT)(RTLBM*, PULONG, ULONG);
typedef VOID (NTAPI *PFN_ALL) (RTLBM*);

static PFN_INIT pInit;
static PFN_ALL  pSetAll, pClearAll;

#define PAD 64
static unsigned char arena[PAD + 512 + PAD];

/* returns the index of the first and last byte that differs from the poison */
static void probe(ULONG bits, const char* which) {
    memset(arena, 0xA5, sizeof arena);
    PULONG buf = (PULONG)(arena + PAD);
    RTLBM bm; pInit(&bm, buf, bits);
    if (which[0] == 'S') pSetAll(&bm); else pClearAll(&bm);

    int first = -1, last = -1;
    unsigned char want = (which[0] == 'S') ? 0xFF : 0x00;
    for (int i = 0; i < (int)sizeof arena; ++i) {
        if (arena[i] != 0xA5) { if (first < 0) first = i; last = i; }
    }
    size_t exact = ((size_t)bits + 7) / 8;
    size_t words = (((size_t)bits + 31) / 32) * 4;

    printf("  %-8s bits=%-9u  ceil(N/8)=%-6zu ceil(N/32)*4=%-6zu  ", which, bits, exact, words);
    if (first < 0) { printf("NOTHING WRITTEN\n"); return; }
    printf("wrote [%d,%d] = %d byte(s)", first - PAD, last - PAD, last - first + 1);
    if (first != PAD) printf("  <<< did not start at Buffer[0]");
    size_t n = (size_t)(last - first + 1);
    if      (n == exact) printf("  -> ceil(N/8)");
    else if (n == words) printf("  -> whole ULONGs");
    else                 printf("  -> NEITHER");

    /* what is in the final partial byte? */
    if (bits % 8 && which[0] == 'S') {
        unsigned char fb = arena[PAD + exact - 1];
        unsigned char inrange = (unsigned char)((1u << (bits % 8)) - 1u);
        printf("  final byte=%02X (in-range mask=%02X)%s", fb, inrange,
               (fb == 0xFF) ? " padding SET" : ((fb == inrange) ? " padding CLEAR" : " padding ??"));
    }
    /* was anything written past ceil(N/8)? */
    if ((size_t)(last - PAD + 1) > exact) printf("  [writes %zu byte(s) PAST ceil(N/8)]", (size_t)(last - PAD + 1) - exact);
    printf("\n");
    (void)want;
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE nt = LoadLibraryW(L"ntdll.dll");
    pInit     = (PFN_INIT)GetProcAddress(nt, "RtlInitializeBitMap");
    pSetAll   = (PFN_ALL) GetProcAddress(nt, "RtlSetAllBits");
    pClearAll = (PFN_ALL) GetProcAddress(nt, "RtlClearAllBits");
    if (!pInit || !pSetAll || !pClearAll) { printf("missing export\n"); return 2; }

    printf("== how many bytes, and what lands in the tail ==\n");
    static const ULONG N[] = { 0,1,2,7,8,9,15,16,17,31,32,33,63,64,65,95,96,97,127,128,129,255,256,257,1000,1024 };
    for (int i = 0; i < (int)(sizeof N / sizeof N[0]); ++i) probe(N[i], "SetAll");
    printf("\n");
    for (int i = 0; i < (int)(sizeof N / sizeof N[0]); ++i) probe(N[i], "ClearAll");

    printf("\n== does SizeOfBitMap=0 touch Buffer at all? ==\n");
    memset(arena, 0xA5, sizeof arena);
    { RTLBM bm; pInit(&bm, (PULONG)(arena+PAD), 0); pSetAll(&bm);
      int touched = 0; for (size_t i = 0; i < sizeof arena; ++i) if (arena[i] != 0xA5) touched++;
      printf("  SetAll(0): %d byte(s) modified\n", touched); }

    printf("\n== is RtlInitializeBitMap doing anything but storing the two fields? ==\n");
    { RTLBM bm; memset(&bm, 0xCC, sizeof bm); pInit(&bm, (PULONG)(arena+PAD), 1234);
      printf("  SizeOfBitMap=%u  Buffer=%p (arena+PAD=%p)\n", bm.SizeOfBitMap, (void*)bm.Buffer, (void*)(arena+PAD)); }

    /* The N + 31 overflow boundary. SizeOfBitMap is a ULONG, so N + 31 wraps a 32-bit register
       above 0xFFFFFFE0. The two candidate implementations diverge enormously there:

         64-bit count  ->  ((N + 31) / 32) * 4  is ~512 MB and the fill runs off the end
         32-bit count  ->  N = 0xFFFFFFFF wraps to 30 / 32 = 0, and NOTHING is written

       One committed page followed by reserved-only address space makes the first case fault within
       a page instead of scribbling through the heap, and __try tells the two apart. */
    printf("\n== N + 31 overflow boundary (N near 2^32) ==\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        unsigned char* base = (unsigned char*)VirtualAlloc(NULL, pg * 16, MEM_RESERVE, PAGE_NOACCESS);
        VirtualAlloc(base, pg, MEM_COMMIT, PAGE_READWRITE);   /* exactly one writable page */
        static const ULONG NN[] = { 0xFFFFFFFFu, 0xFFFFFFE1u, 0xFFFFFFE0u };
        for (int i = 0; i < 3; ++i) {
            memset(base, 0xA5, pg);
            RTLBM bm; bm.SizeOfBitMap = NN[i]; bm.Buffer = (PULONG)base;
            int faulted = 0;
            __try { pSetAll(&bm); } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
            int touched = 0;
            for (SIZE_T k = 0; k < pg; ++k) if (base[k] != 0xA5) { touched = 1; break; }
            printf("  N=0x%08X  %-22s first byte %s\n", NN[i],
                   faulted ? "FAULTED (64-bit count)" : "returned (32-bit wrap)",
                   touched ? "written" : "untouched");
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }
    return 0;
}
