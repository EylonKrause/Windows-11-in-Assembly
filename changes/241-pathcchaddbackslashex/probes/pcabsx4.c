/* changes/241-pathcchaddbackslashex/probes/pcabsx4.c
   THE AddBackslashEx cch CEILING, which is not a power of two.

   pcabsx3.c bisected it and got a boundary that should not exist: the largest accepted cch is
   0x80000005 (2 147 483 653) and the smallest rejected is 0x80000006. That is 2^31 + 5, which is not
   any limit anyone writes down, so either the acceptance is not monotonic in cch or the boundary moves
   with something else -- most likely the length of the string, since the only other quantity in play
   is n.

   If the boundary moves with n the rule is an arithmetic one (cch + n against some constant, or a
   32-bit truncation somewhere). This file sweeps the boundary at several lengths and reports it
   against a few candidate formulas, and also checks monotonicity so the bisection cannot have been
   lying.

   This matters because the project's gate is bit-exact HRESULTs, and a caller that passes a cch this
   large is exactly the caller a reimplementation would silently disagree with. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef HRESULT (WINAPI *PEX)(PWSTR, size_t, PWSTR*, size_t*);
static PEX addx, remx;
static wchar_t buf[600];

static int accepts(PEX f, int n, size_t cch)
{
    for (int i = 0; i < n; ++i) buf[i] = (wchar_t)(L'a' + i % 23);
    buf[n] = 0;
    PWSTR e; size_t r;
    HRESULT hr = f(buf, cch, &e, &r);
    return hr == S_OK || hr == S_FALSE;
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    addx = (PEX)GetProcAddress(hk, "PathCchAddBackslashEx");
    remx = (PEX)GetProcAddress(hk, "PathCchRemoveBackslashEx");
    if (!addx) { printf("cannot resolve\n"); return 1; }

    printf("=== 1. is acceptance MONOTONIC in cch? ===\n");
    printf("  Sweeping 0x7FFFFFF0..0x80000020 for a 6-character path; a single flip back to accepted\n");
    printf("  after a rejection would mean the bisection in pcabsx3.c was meaningless.\n");
    {
        int last = -1, flips = 0;
        for (size_t cch = 0x7FFFFFF0; cch <= 0x80000020; ++cch) {
            int a = accepts(addx, 6, cch);
            if (last >= 0 && a != last) {
                printf("    boundary at cch=%#zx: %s -> %s\n", cch,
                       last ? "accepted" : "rejected", a ? "accepted" : "rejected");
                ++flips;
            }
            last = a;
        }
        printf("    %d transitions in that range%s\n", flips,
               flips == 1 ? " (monotonic)" : " (NOT monotonic -- the bisection was invalid)");
    }

    printf("\n=== 2. DOES THE BOUNDARY MOVE WITH THE STRING LENGTH? ===\n");
    printf("  %6s %14s %14s %14s\n", "n", "last accepted", "+n", "+n+1");
    {
        for (int n = 0; n <= 40; n += 4) {
            size_t lo = 1, hi = (size_t)1 << 33;
            /* bisect, trusting monotonicity established above */
            while (lo + 1 < hi) {
                size_t mid = lo + (hi - lo) / 2;
                if (accepts(addx, n, mid)) lo = mid; else hi = mid;
            }
            printf("  %6d %#14zx %#14zx %#14zx\n", n, lo, lo + (size_t)n, lo + (size_t)n + 1);
        }
        printf("  => if the last-accepted value FALLS as n rises, the rule is arithmetic in n\n");
    }

    printf("\n=== 3. the same for RemoveBackslashEx, which appeared to have no ceiling ===\n");
    {
        for (int n = 0; n <= 16; n += 8) {
            int all = 1;
            static const size_t V[] = { (size_t)1 << 31, (size_t)1 << 32, (size_t)1 << 40,
                                        (size_t)1 << 62, (size_t)-1 };
            printf("  n=%d:", n);
            for (int i = 0; i < 5; ++i) {
                int a = accepts(remx, n, V[i]);
                printf("  %#zx:%s", V[i], a ? "ok" : "REJ");
                if (!a) all = 0;
            }
            printf("%s\n", all ? "   (no ceiling)" : "");
        }
    }

    printf("\n=== 4. and what does AddBackslashEx do just below its boundary? ===\n");
    printf("  The interesting question is whether it WRITES with a cch that large, or merely accepts.\n");
    {
        for (int k = 0; k < 4; ++k) {
            size_t cch = (size_t)0x80000003 + k;
            for (int i = 0; i < 6; ++i) buf[i] = (wchar_t)(L'a' + i);
            buf[6] = 0;
            for (int i = 7; i < 20; ++i) buf[i] = 0xCDCD;
            PWSTR e = (PWSTR)(size_t)0xDEAD; size_t r = 0xDEAD;
            HRESULT hr = addx(buf, cch, &e, &r);
            printf("  cch=%#zx -> %08lX  \"%ls\"  end=%d rem=%#zx\n", cch, (unsigned long)hr, buf,
                   (size_t)e == 0xDEAD ? -1 : (e ? (int)(e - buf) : -2), r);
        }
    }
    return 0;
}
