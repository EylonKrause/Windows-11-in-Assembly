// changes/293-systemtimetofiletime/probes/abi.c
// Gate 3, dynamic, for change 293. tools/abi-check/check.bat carries a hardcoded list of change
// directories and this change is not in it (and no existing file may be edited), so this driver
// reuses tools/abi-check/abi_probe.asm UNCHANGED -- it is only assembled into this directory -- and
// arms the sentinels AROUND THE CALL with wia_abi_call4, which is the form abi_check.c documents as
// the one that cannot be masked by a compiled thunk saving and restoring a register itself.
//
// Bits 0-7 = rbx rbp rdi rsi r12 r13 r14 r15;  bits 8-17 = xmm6..xmm15 (low 128 bits only);
// bit 18 = stack pointer not restored;  bit 19 = direction flag left set.
//
// Five call shapes, so both exits and both February paths are covered: success in a 31-day month,
// success in February of a leap year, success in February of a non-leap year, rejection by the
// vector range check, and rejection by the day-vs-month-length check (which is the one that leaves
// through st_fail after the SetLastError call, i.e. after the only CALL this implementation makes).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef struct { unsigned short wYear,wMonth,wDayOfWeek,wDay,wHour,wMinute,wSecond,wMilliseconds; } WIA_ST;
extern int wia_systemtime_to_filetime(const WIA_ST*, unsigned long long*);
extern unsigned long long wia_abi_probe(void (*thunk)(void));
extern unsigned long long wia_abi_call4(void* fn, unsigned long long a, unsigned long long b,
                                        unsigned long long c, unsigned long long d);

static const char* NAMES[20] = {
    "rbx","rbp","rdi","rsi","r12","r13","r14","r15",
    "xmm6","xmm7","xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15",
    "rsp-not-restored","direction-flag-set"
};

static WIA_ST cases[5] = {
    {2023, 6,4,15,13,45, 7,123},   /* plain success                       */
    {2024, 2,4,29,23,59,59,999},   /* February, leap year  (st_feb path)  */
    {2023, 2,2,28,23,59,59,999},   /* February, not leap   (st_feb path)  */
    {2023,13,4,15,13,45, 7,123},   /* rejected by the vector range check  */
    {2023, 6,4,31,13,45, 7,123}    /* rejected by the month-length check  */
};
static const char* CASENAME[5] = {"plain","feb-leap","feb-nonleap","bad-month","bad-day"};
static volatile long long sink;
static void thunk(void){ unsigned long long t=0; sink += wia_systemtime_to_filetime(&cases[0], &t) + (long long)t; }

int main(void){
    unsigned long long mask = 0;
    for(int i=0;i<5;++i){
        unsigned long long t = 0;
        unsigned long long m = wia_abi_call4((void*)wia_systemtime_to_filetime,
                                             (unsigned long long)(void*)&cases[i],
                                             (unsigned long long)(void*)&t, 0, 0);
        if(m) printf("  case %-12s mask=0x%llX\n", CASENAME[i], m);
        mask |= m;
    }
    mask |= wia_abi_probe(thunk);
    if(!mask){ printf("ABI (dynamic, 5 call shapes + whole-thunk): PASS -- every non-volatile register survived\n"); return 0; }
    printf("ABI (dynamic): FAIL mask=0x%llX ->", mask);
    for(int b=0;b<20;++b) if(mask & (1ULL<<b)) printf(" %s", NAMES[b]);
    printf("\n");
    return 1;
}
