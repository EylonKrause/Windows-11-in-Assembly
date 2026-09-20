/* changes/292-filetimetosystemtime/probes/leapdata.c
 *
 * Throwaway. The disassembly of ntdll!RtlpTimeToTimeFields (which is what
 * kernelbase!FileTimeToSystemTime calls, and which ntdll!RtlTimeToTimeFields is a two-instruction
 * thunk onto) begins with a LEAP-SECOND test:
 *
 *      mov  rax, gs:[60h]                  ; PEB
 *      mov  r10, [rax+7B8h]                ; PEB.LeapSecondData
 *      test r10,r10   / je  plain_body
 *      cmp  byte ptr [r10], 0 / je plain_body
 *      mov  ebx, [r10+4]                   ; leap-second COUNT
 *      ...
 *      test ebx,ebx / jne  scan_leap_table
 *
 * contract.c found LeapSecondData NON-NULL on this machine, so the first two tests do NOT take the
 * plain body -- the inlined body runs instead. This probe reads the structure to find out whether
 * the COUNT is zero, i.e. whether the leap-second scan is ever entered, because if it is not then
 * the arithmetic that actually executes is the unmodified civil-from-days path and change 126's
 * engine is a legal substitute. If the count were non-zero the contract would have to change.
 *
 * cl /nologo /O2 leapdata.c
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <intrin.h>

int main(void)
{
    unsigned char* peb = (unsigned char*)__readgsqword(0x60);
    unsigned char* lsd = *(unsigned char**)(peb + 0x7B8);
    unsigned flags     = *(unsigned*)(peb + 0x7C0);
    unsigned i;

    printf("PEB                = %p\n", (void*)peb);
    printf("PEB.LeapSecondData = %p\n", (void*)lsd);
    printf("PEB.LeapSecondFlags= %08X   (bit0 = SixtySecondEnabled -> r8d&1 in the body)\n", flags);
    if (!lsd) { printf("NULL -> plain body. done.\n"); return 0; }

    printf("[lsd+0] (byte, the 'valid' gate) = %u  -> %s\n", lsd[0],
           lsd[0] ? "inlined body runs" : "plain body runs");
    printf("[lsd+4] (dword, the COUNT)       = %u  -> %s\n", *(unsigned*)(lsd + 4),
           *(unsigned*)(lsd + 4) ? "*** leap-second scan ENTERED ***"
                                 : "scan skipped: arithmetic is the unmodified path");
    printf("raw 64 bytes:");
    for (i = 0; i < 64; ++i) { if (!(i & 15)) printf("\n  +%02X:", i); printf(" %02X", lsd[i]); }
    printf("\n");

    /* Does any second ever come back as 60 on this machine? A leap second is the only way. */
    {
        typedef BOOL (WINAPI *FT2ST)(const FILETIME*, LPSYSTEMTIME);
        FT2ST f = (FT2ST)GetProcAddress(LoadLibraryW(L"kernel32.dll"), "FileTimeToSystemTime");
        long long t; int sixty = 0;
        /* every minute boundary minus one tick across 1972..2024, where every real leap second is */
        for (t = 117000000000000000LL; t < 133800000000000000LL; t += 600000000LL) {
            SYSTEMTIME st; FILETIME ft; long long q = t - 1;
            memcpy(&ft, &q, 8);
            f(&ft, &st);
            if (st.wSecond >= 60) { printf("wSecond=%u at t=%lld\n", st.wSecond, q); ++sixty; }
        }
        printf("minute boundaries 1972..2024 with wSecond>=60: %d\n", sixty);
    }
    return 0;
}
