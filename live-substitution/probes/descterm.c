/* probes/descterm.c -- the structural follow-up to the converter finding.
 *
 * live_subst_ntconv2.c found five ntdll converters writing a NUL terminator at [Length] and
 * therefore needing one element MORE than the conversion itself -- a rule none of them had, in
 * either the implementation or the oracle. The obvious next question is whether any OTHER landed
 * change that fills a caller's counted-string descriptor has the same rule and the same gap.
 *
 * Three candidates are covered by the ORIGINAL live_subst.c, whose comparison has both of the
 * blind spots the converter gates had:
 *
 *     U_STR du = {0, (unsigned short)(n*2), o1};        <- MaximumLength never varied
 *     for(int i=0;i<du.Length/2 && !bad;i++) ...        <- comparison bounded by Length
 *
 * and which, worse, compares the patched export against the REFERENCE rather than against the
 * shipped export -- so an implementation and an oracle that are wrong together look right.
 * Being "live covered" is binary; it says nothing about what the corpus asks.
 *
 *   015 RtlUpcaseUnicodeString   -- the twin of 017 RtlDowncaseUnicodeString, which does NOT
 *                                   terminate. Twins are not evidence: 018 and 020 are twins and
 *                                   have opposite failure disciplines.
 *   052 RtlIntegerToUnicodeString
 *   053 RtlInt64ToUnicodeString  -- integer formatters filling a UNICODE_STRING, the classic
 *                                   place for a terminator.
 *
 * This asks each of them directly, walking MaximumLength across the boundary and printing every
 * position that stopped being poison.
 */
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef LONG NTSTATUS_;
typedef struct { USHORT Length, MaximumLength; WCHAR* Buffer; } USTR;

typedef NTSTATUS_ (NTAPI *fnUU)(USTR*, const USTR*, BOOLEAN);
typedef NTSTATUS_ (NTAPI *fnI2U)(ULONG, ULONG, USTR*);
typedef NTSTATUS_ (NTAPI *fnI642U)(LONGLONG, ULONG, USTR*);

#define CAP 64
#define NP  0x71

static void report(const char* tag, int arg, int maxb, NTSTATUS_ r, USTR* d, unsigned char* buf){
    int k, last=-1;
    for(k=0;k<CAP;++k) if(buf[k]!=NP) last=k;
    printf("  %-30s arg=%-4d Max=%-3d -> %08lX Len=%-5u wrote..%-3d  ",
           tag, arg, maxb, (unsigned long)r, d->Length, last);
    for(k=0;k<=last && k<14;++k) printf("%02X ", buf[k]);
    printf("\n");
}

static void ups_row(fnUU f, const WCHAR* src, int n, int maxb){
    unsigned char buf[CAP]; USTR s,d; NTSTATUS_ r;
    memset(buf,NP,sizeof buf);
    s.Length=(USHORT)(2*n); s.MaximumLength=(USHORT)(2*n); s.Buffer=(WCHAR*)src;
    d.Length=0x5A5A; d.MaximumLength=(USHORT)maxb; d.Buffer=(WCHAR*)buf;
    r=f(&d,&s,FALSE);
    report("RtlUpcaseUnicodeString",n,maxb,r,&d,buf);
}

static void i2u_row(fnI2U f, ULONG v, ULONG base, int maxb){
    unsigned char buf[CAP]; USTR d; NTSTATUS_ r;
    memset(buf,NP,sizeof buf);
    d.Length=0x5A5A; d.MaximumLength=(USHORT)maxb; d.Buffer=(WCHAR*)buf;
    r=f(v,base,&d);
    report("RtlIntegerToUnicodeString",(int)v,maxb,r,&d,buf);
}

static void i642u_row(fnI642U f, LONGLONG v, ULONG base, int maxb){
    unsigned char buf[CAP]; USTR d; NTSTATUS_ r;
    memset(buf,NP,sizeof buf);
    d.Length=0x5A5A; d.MaximumLength=(USHORT)maxb; d.Buffer=(WCHAR*)buf;
    r=f(v,base,&d);
    report("RtlInt64ToUnicodeString",(int)v,maxb,r,&d,buf);
}

int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    fnUU    ups =(fnUU)   GetProcAddress(h,"RtlUpcaseUnicodeString");
    fnI2U   i2u =(fnI2U)  GetProcAddress(h,"RtlIntegerToUnicodeString");
    fnI642U i64 =(fnI642U)GetProcAddress(h,"RtlInt64ToUnicodeString");
    static const WCHAR ws[]=L"abcd";
    int m;
    if(!ups||!i2u||!i64){ printf("resolve failed\n"); return 2; }
    printf("NP = 0x71 poison. A 4-character upcase needs 8 bytes; \"1234\" needs 8 bytes.\n");
    printf("If Max == exactly-enough SUCCEEDS and nothing is written past the string, there is no\n");
    printf("terminator. If it FAILS, or a NUL appears at [Length], there is.\n\n");

    printf("015 RtlUpcaseUnicodeString -- twin of 017, which does NOT terminate\n");
    for(m=4;m<=12;m+=2) ups_row(ups,ws,4,m);
    ups_row(ups,ws,0,0);
    ups_row(ups,ws,0,2);

    printf("\n052 RtlIntegerToUnicodeString (base 10)\n");
    for(m=4;m<=14;m+=2) i2u_row(i2u,1234,10,m);
    printf("  and a single digit, where the string is 2 bytes\n");
    for(m=0;m<=6;m+=2) i2u_row(i2u,7,10,m);
    printf("  and an ODD MaximumLength\n");
    i2u_row(i2u,1234,10,9); i2u_row(i2u,1234,10,11);

    printf("\n053 RtlInt64ToUnicodeString (base 10)\n");
    for(m=4;m<=14;m+=2) i642u_row(i64,1234,10,m);
    for(m=0;m<=6;m+=2) i642u_row(i64,7,10,m);

    return 0;
}
