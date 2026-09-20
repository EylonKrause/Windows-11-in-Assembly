/* changes/292-filetimetosystemtime/probes/abi.c
 *
 * Gate 3, dynamic. Drives probes/abi.asm over both paths this change has -- the accept path, which
 * is a leaf, and the reject path, which builds a shadow frame and calls SetLastError -- and reports
 * any non-volatile register that did not survive the call.
 *
 * It exists because the repository's own dynamic gate lives in tools/abi-check/check.bat, and
 * adding a change to it means editing that file and abi_check.c. This change may not modify an
 * existing file, so the probe is here instead. tools/abi-audit.py (the static half of gate 3)
 * covers this file's impl.asm the same as every other.
 *
 *   ml64 /nologo /c abi.asm  &&  cl /nologo /O2 abi.c abi.obj ..\impl.obj kernel32.lib
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

void wia_292_abi(unsigned char* out, const FILETIME* ft, SYSTEMTIME* st);

static const char* GPR[8] = { "rbx", "rbp", "rsi", "rdi", "r12", "r13", "r14", "r15" };

static int check(const char* what, long long t)
{
    unsigned char out[0xE0];
    unsigned long long* q = (unsigned long long*)out;
    FILETIME ft; SYSTEMTIME st;
    int i, bad = 0;

    memcpy(&ft, &t, 8);
    memset(out, 0, sizeof out);
    memset(&st, 0, sizeof st);
    wia_292_abi(out, &ft, &st);

    for (i = 0; i < 8; ++i)
        if (q[i] != 0xA5A5A5A5A5A50010ull + (unsigned)i) {
            printf("   CLOBBERED %-4s expected %016llX got %016llX\n",
                   GPR[i], 0xA5A5A5A5A5A50010ull + (unsigned)i, q[i]);
            ++bad;
        }
    for (i = 0; i < 10; ++i) {
        unsigned long long lo = q[8 + i * 2], hi = q[9 + i * 2];
        unsigned long long elo = 0xC0DEBA5E00000006ull + (unsigned)i;
        unsigned long long ehi = 0xF00DFACE00000006ull + (unsigned)i;
        if (lo != elo || hi != ehi) {
            printf("   CLOBBERED xmm%-2d low 128 bits: expected %016llX%016llX got %016llX%016llX\n",
                   6 + i, ehi, elo, hi, lo);
            ++bad;
        }
    }
    printf("   %-34s %s\n", what, bad ? "FAIL" : "all 8 GPRs and xmm6-xmm15 survived");
    return bad;
}

int main(void)
{
    int bad = 0;
    printf("ABI (dynamic), wia_filetime_to_systemtime:\n");
    bad += check("accept path (2023)",        133200000000000000LL);
    bad += check("accept path (epoch 0)",     0LL);
    bad += check("accept path (max)",         0x7FFFFFFFFFFFFFFFLL);
    bad += check("reject path (-1, calls SetLastError)", -1LL);
    bad += check("reject path (INT64_MIN)",   (long long)0x8000000000000000ULL);
    printf("ABI DYNAMIC: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
