/* discovery/runencode_seed.c
   Is RtlRunEncodeUnicodeString a function of its input, or does it invent a seed?

   discovery/runencode_contract.c raised the question and could not settle it. Two calls with
   Seed = 0 made back to back produced identical output and returned seed F9; a third call with
   Seed = 0 later in the same process produced different output and returned seed 20. Both readings
   are consistent with "the seed is taken from the clock when the caller passes zero", and that
   single fact decides whether this export can be converted at all: a routine whose output is not a
   function of its input cannot be reimplemented bit-exactly, and this repository parks rather than
   ships an approximation.

   Two things separate the possibilities, and neither takes long:

     * call with Seed = 0 repeatedly with a real delay in between. A clock-derived seed changes.
     * call with a fixed NON-zero seed repeatedly with the same delay. If those agree while the
       zero-seed calls do not, the transform itself is deterministic and only the seed selection
       is not, which is a much narrower problem than it first looks.
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef LONG (NTAPI *PFN_ENC)(PUCHAR, USTR*);

static PFN_ENC pEnc;

#define N 8
static void run(UCHAR seed_in, UCHAR* seed_out, wchar_t* out) {
    static const wchar_t src[N] = { L'A', L'B', L'C', L'D', L'E', L'F', L'G', L'H' };
    memcpy(out, src, sizeof src);
    USTR u = { (USHORT)(N*2), (USHORT)(N*2), out };
    UCHAR s = seed_in;
    pEnc(&s, &u);
    *seed_out = s;
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    pEnc = (PFN_ENC)GetProcAddress(LoadLibraryW(L"ntdll.dll"), "RtlRunEncodeUnicodeString");
    if (!pEnc) { printf("missing export\n"); return 2; }

    wchar_t o[N]; UCHAR s;

    printf("== Seed = 0, six calls, 120 ms apart ==\n");
    UCHAR seeds[6]; wchar_t outs[6][N];
    for (int i = 0; i < 6; ++i) {
        run(0, &seeds[i], outs[i]);
        printf("  call %d: seed out=%02X  out[0..3]=%04X %04X %04X %04X\n",
               i, seeds[i], outs[i][0], outs[i][1], outs[i][2], outs[i][3]);
        Sleep(120);
    }
    int allsame = 1;
    for (int i = 1; i < 6; ++i)
        if (seeds[i] != seeds[0] || memcmp(outs[i], outs[0], sizeof outs[0])) allsame = 0;
    printf("  -> %s\n", allsame ? "all identical: seed 0 is NOT clock derived"
                                : "they DIFFER: with Seed = 0 the routine invents one");

    printf("\n== Seed = 0x5A, six calls, 120 ms apart ==\n");
    UCHAR s2[6]; wchar_t o2[6][N];
    for (int i = 0; i < 6; ++i) {
        run(0x5A, &s2[i], o2[i]);
        printf("  call %d: seed out=%02X  out[0..3]=%04X %04X %04X %04X\n",
               i, s2[i], o2[i][0], o2[i][1], o2[i][2], o2[i][3]);
        Sleep(120);
    }
    int fixedsame = 1;
    for (int i = 1; i < 6; ++i)
        if (s2[i] != s2[0] || memcmp(o2[i], o2[0], sizeof o2[0])) fixedsame = 0;
    printf("  -> %s\n", fixedsame ? "all identical: the TRANSFORM is deterministic given a seed"
                                  : "they DIFFER: not even a fixed seed makes it reproducible");

    printf("\n== is every non-zero seed self-returning and stable? ==\n");
    {
        int bad = 0;
        for (int v = 1; v < 256; ++v) {
            UCHAR sa, sb; wchar_t oa[N], ob[N];
            run((UCHAR)v, &sa, oa);
            run((UCHAR)v, &sb, ob);
            if (sa != (UCHAR)v || sb != (UCHAR)v || memcmp(oa, ob, sizeof oa)) { ++bad; if (bad < 5)
                printf("  seed %02X: out %02X/%02X %s\n", v, sa, sb, memcmp(oa,ob,sizeof oa) ? "payload differs" : ""); }
        }
        printf("  255 non-zero seeds: %d misbehaved\n", bad);
    }

    (void)o; (void)s;
    return 0;
}
