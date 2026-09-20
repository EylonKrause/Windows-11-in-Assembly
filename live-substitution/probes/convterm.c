/* convterm.c: do the ntdll ANSI/OEM converters NUL-TERMINATE, and does that change the size rule?
 *
 * The live harness found 018/019/020/024/025 diverging while 017 (Unicode -> Unicode) and 165 did
 * not. Two symptoms, both pointing the same way:
 *   * with MaximumLength exactly the converted length, the export answers STATUS_BUFFER_OVERFLOW
 *     (0x80000005) where these implementations answer STATUS_SUCCESS, so the export wants one
 *     more element than the conversion itself needs;
 *   * with a generous MaximumLength both answer STATUS_SUCCESS and the BUFFERS still differ.
 *
 * Together those say "it writes a terminator". This measures it exactly: for each export, walk
 * MaximumLength across the interesting range and print the status, the resulting Length, and every
 * byte position that stopped being poison, so "wrote a terminator" is separated from "wrote one
 * element too many", and the OVERFLOW path's partial write is recorded rather than guessed.
 */
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef LONG NTSTATUS_;
typedef struct { USHORT Length, MaximumLength; WCHAR* Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; CHAR*  Buffer; } ASTR;

typedef NTSTATUS_ (NTAPI *fnAU)(ASTR*, const USTR*, BOOLEAN);  /* wide  -> narrow */
typedef NTSTATUS_ (NTAPI *fnUA)(USTR*, const ASTR*, BOOLEAN);  /* narrow-> wide   */
typedef NTSTATUS_ (NTAPI *fnUU)(USTR*, const USTR*, BOOLEAN);  /* wide  -> wide   */

#define CAP 64
#define NP  0x71

static void wide_row(const char* name, fnAU f, const WCHAR* src, int n, int maxn){
    unsigned char buf[CAP]; USTR s; ASTR d; NTSTATUS_ r; int k, last=-1;
    memset(buf,NP,sizeof buf);
    s.Length=(USHORT)(2*n); s.MaximumLength=(USHORT)(2*n); s.Buffer=(WCHAR*)src;
    d.Length=0x5A5A; d.MaximumLength=(USHORT)maxn; d.Buffer=(CHAR*)buf;
    r=f(&d,&s,FALSE);
    for(k=0;k<CAP;++k) if(buf[k]!=NP) last=k;
    printf("  %-34s srclen=%-3d Max=%-3d -> %08lX Len=%-5u wrote..%-3d  ",
           name, n, maxn, (unsigned long)r, d.Length, last);
    for(k=0;k<=last && k<12;++k) printf("%02X ", buf[k]);
    printf("\n");
}

static void narrow_row(const char* name, fnUA f, const char* src, int n, int maxw){
    unsigned char buf[CAP]; ASTR s; USTR d; NTSTATUS_ r; int k, last=-1;
    memset(buf,NP,sizeof buf);
    s.Length=(USHORT)n; s.MaximumLength=(USHORT)n; s.Buffer=(CHAR*)src;
    d.Length=0x5A5A; d.MaximumLength=(USHORT)maxw; d.Buffer=(WCHAR*)buf;
    r=f(&d,&s,FALSE);
    for(k=0;k<CAP;++k) if(buf[k]!=NP) last=k;
    printf("  %-34s srclen=%-3d Max=%-3d -> %08lX Len=%-5u wrote..%-3d  ",
           name, n, maxw, (unsigned long)r, d.Length, last);
    for(k=0;k<=last && k<12;++k) printf("%02X ", buf[k]);
    printf("\n");
}

static void ww_row(const char* name, fnUU f, const WCHAR* src, int n, int maxw){
    unsigned char buf[CAP]; USTR s, d; NTSTATUS_ r; int k, last=-1;
    memset(buf,NP,sizeof buf);
    s.Length=(USHORT)(2*n); s.MaximumLength=(USHORT)(2*n); s.Buffer=(WCHAR*)src;
    d.Length=0x5A5A; d.MaximumLength=(USHORT)maxw; d.Buffer=(WCHAR*)buf;
    r=f(&d,&s,FALSE);
    for(k=0;k<CAP;++k) if(buf[k]!=NP) last=k;
    printf("  %-34s srclen=%-3d Max=%-3d -> %08lX Len=%-5u wrote..%-3d  ",
           name, n, maxw, (unsigned long)r, d.Length, last);
    for(k=0;k<=last && k<12;++k) printf("%02X ", buf[k]);
    printf("\n");
}

int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    fnAU u2a =(fnAU)GetProcAddress(h,"RtlUnicodeStringToAnsiString");
    fnAU u2au=(fnAU)GetProcAddress(h,"RtlUpcaseUnicodeStringToAnsiString");
    fnAU u2oe=(fnAU)GetProcAddress(h,"RtlUnicodeStringToOemString");
    fnUA a2u =(fnUA)GetProcAddress(h,"RtlAnsiStringToUnicodeString");
    fnUA oe2u=(fnUA)GetProcAddress(h,"RtlOemStringToUnicodeString");
    fnUU down=(fnUU)GetProcAddress(h,"RtlDowncaseUnicodeString");
    static const WCHAR ws[]=L"AbCdEfGh";
    static const char  ns[]="AbCdEfGh";
    int m;
    if(!u2a||!u2au||!u2oe||!a2u||!oe2u||!down){ printf("resolve failed\n"); return 2; }

    printf("A 4-character source; the conversion itself needs 4 bytes (narrow) or 8 (wide).\n");
    printf("NP = 0x71 poison. 'wrote..k' is the last index that stopped being poison.\n\n");

    printf("RtlUnicodeStringToAnsiString (wide -> narrow)\n");
    for(m=0;m<=8;++m) wide_row("RtlUnicodeStringToAnsiString",u2a,ws,4,m);

    printf("\nRtlUpcaseUnicodeStringToAnsiString\n");
    for(m=3;m<=6;++m) wide_row("RtlUpcaseUnicodeStringToAnsiString",u2au,ws,4,m);

    printf("\nRtlUnicodeStringToOemString\n");
    for(m=3;m<=6;++m) wide_row("RtlUnicodeStringToOemString",u2oe,ws,4,m);

    printf("\nRtlAnsiStringToUnicodeString (narrow -> wide)\n");
    for(m=0;m<=14;m+=2) narrow_row("RtlAnsiStringToUnicodeString",a2u,ns,4,m);
    narrow_row("RtlAnsiStringToUnicodeString",a2u,ns,4,9);   /* an ODD MaximumLength */
    narrow_row("RtlAnsiStringToUnicodeString",a2u,ns,4,11);

    printf("\nRtlOemStringToUnicodeString\n");
    for(m=6;m<=12;m+=2) narrow_row("RtlOemStringToUnicodeString",oe2u,ns,4,m);

    printf("\nRtlDowncaseUnicodeString (wide -> wide) -- the one that did NOT diverge\n");
    for(m=6;m<=12;m+=2) ww_row("RtlDowncaseUnicodeString",down,ws,4,m);

    printf("\nAn empty source, where a terminator is the only thing that could be written\n");
    wide_row("RtlUnicodeStringToAnsiString",u2a,ws,0,0);
    wide_row("RtlUnicodeStringToAnsiString",u2a,ws,0,1);
    narrow_row("RtlAnsiStringToUnicodeString",a2u,ns,0,0);
    narrow_row("RtlAnsiStringToUnicodeString",a2u,ns,0,2);
    ww_row("RtlDowncaseUnicodeString",down,ws,0,0);

    printf("\n(A) is 018's partial write min(srclen, Max-1) for a LONGER source too?\n");
    { static const WCHAR L8[]=L"ABCDEFGH"; int m;
      for(m=0;m<=10;++m) wide_row("u2a len 8",u2a,L8,8,m); }

    printf("\n(B) srclen 0 at Max 0 and 1 for the four that write NOTHING on overflow\n");
    wide_row("u2au",u2au,ws,0,0);   wide_row("u2au",u2au,ws,0,1);
    wide_row("u2oem",u2oe,ws,0,0);  wide_row("u2oem",u2oe,ws,0,1);
    narrow_row("a2u",a2u,ns,0,0);   narrow_row("a2u",a2u,ns,0,1);
    narrow_row("a2u",a2u,ns,0,2);
    narrow_row("oem2u",oe2u,ns,0,0);narrow_row("oem2u",oe2u,ns,0,2);

    printf("\n(C) 017 downcase: srclen 0, and Max exactly 2n, for contrast\n");
    ww_row("downcase",down,ws,0,1);  ww_row("downcase",down,ws,2,4);
    ww_row("downcase",down,ws,2,3);

    return 0;
}
