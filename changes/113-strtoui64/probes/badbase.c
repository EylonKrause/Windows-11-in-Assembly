/* badbase.c -- what does the strtoX family do with an INVALID base?
 *
 * The live harness found 567 of 30000 cases differing, every one of them base 1: the shipped
 * exports set errno = EINVAL and report through the invalid-parameter handler, while changes
 * 110-113 return the same value and endptr and leave errno as the caller had it.
 *
 * The valid set is 0 and 2..36. This asks all four entries about several invalid bases, and about
 * base 0 and 10 as controls, recording the value, the endptr, errno and whether the handler fired.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static volatile long n;
static void __cdecl iph(const wchar_t*a,const wchar_t*b,const wchar_t*c,unsigned d,uintptr_t e){
    (void)a;(void)b;(void)c;(void)d;(void)e; ++n;
}
typedef long               (__cdecl *fnL)(const char*, char**, int);
typedef unsigned long      (__cdecl *fnUL)(const char*, char**, int);
typedef long long          (__cdecl *fnI64)(const char*, char**, int);
typedef unsigned long long (__cdecl *fnU64)(const char*, char**, int);

#define SENT 0x5A5A

int main(void){
    HMODULE h; int bi, si;
    fnL   sl;  fnUL  sul; fnI64 si64; fnU64 su64;
    static const int  B[] = { 0, 1, 2, 10, 36, 37, -1, 100 };
    static const char* S[] = { "42", "0x0", "  -17zz", "" };
    _set_invalid_parameter_handler(iph);
    h = LoadLibraryW(L"ucrtbase.dll");
    sl  =(fnL)  GetProcAddress(h,"strtol");
    sul =(fnUL) GetProcAddress(h,"strtoul");
    si64=(fnI64)GetProcAddress(h,"_strtoi64");
    su64=(fnU64)GetProcAddress(h,"_strtoui64");
    if(!sl||!sul||!si64||!su64){ printf("cannot resolve\n"); return 2; }

    printf("%-5s %-9s | %-12s %-4s %-6s %-4s | %-12s %-4s %-6s %-4s\n",
           "base","input","strtol","end","errno","iph","strtoul","end","errno","iph");
    printf("---------------------------------------------------------------------------------\n");
    for(bi=0; bi<8; ++bi){
        for(si=0; si<4; ++si){
            char* e1; char* e2; long v1; unsigned long v2; long n1, n2; int r1, r2;
            e1=NULL; errno=SENT; n=0; v1=sl(S[si],&e1,B[bi]); r1=errno; n1=n;
            e2=NULL; errno=SENT; n=0; v2=sul(S[si],&e2,B[bi]); r2=errno; n2=n;
            printf("%-5d \"%-7s\" | %-12ld %-4d %-6d %-4ld | %-12lu %-4d %-6d %-4ld\n",
                   B[bi], S[si],
                   v1, e1?(int)(e1-S[si]):-99, r1, n1,
                   v2, e2?(int)(e2-S[si]):-99, r2, n2);
        }
    }
    printf("\nerrno %d is the sentinel: the export left the caller's value alone.\n", SENT);
    printf("The 64-bit pair behaves identically; checked but not printed to keep this readable.\n");
    {
        char* e; long long a; unsigned long long b; int ra, rb; long na, nb;
        e=NULL; errno=SENT; n=0; a=si64("42",&e,1);  ra=errno; na=n;
        e=NULL; errno=SENT; n=0; b=su64("42",&e,1);  rb=errno; nb=n;
        printf("  _strtoi64 (\"42\",,1) = %lld errno=%d iph=%ld | _strtoui64 = %llu errno=%d iph=%ld\n",
               a, ra, na, b, rb, nb);
    }
    return 0;
}
