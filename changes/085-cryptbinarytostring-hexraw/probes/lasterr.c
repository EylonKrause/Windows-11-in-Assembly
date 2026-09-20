/* b2serr.c -- what last error does CryptBinaryToString{A,W} leave, and on which paths?
 *
 * Live substitution found 19 of 8000 cases where the shipped export sets a last error on the
 * cb == 0 path and changes 081/085/090/092 leave the caller's value untouched. Before changing
 * eight implementations, this establishes the rule: which failures set an error, which value, and
 * whether SUCCESS leaves the caller's value alone (the property change 289 verifies with a
 * sentinel before every call).
 */
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define SENT 0xD1CEu
typedef BOOL (WINAPI *fnA)(const BYTE*, DWORD, DWORD, char*, DWORD*);
typedef BOOL (WINAPI *fnW)(const BYTE*, DWORD, DWORD, wchar_t*, DWORD*);

int main(void){
    HMODULE h=LoadLibraryW(L"crypt32.dll");
    fnA a=(fnA)GetProcAddress(h,"CryptBinaryToStringA");
    fnW w=(fnW)GetProcAddress(h,"CryptBinaryToStringW");
    static const DWORD F[6]={0x0,0x1,0x4,0xc,0x40000001u,0x4000000cu};
    static unsigned char ob[4096]; static wchar_t ow[4096];
    BYTE bin[16]; DWORD n; int k;
    for(k=0;k<16;++k) bin[k]=(BYTE)k;
    if(!a||!w){ printf("cannot resolve\n"); return 2; }

    printf("%-12s %-22s %-6s %-8s %s\n","flags","case","ret","lasterr","*pcch");
    printf("--------------------------------------------------------------\n");
    for(k=0;k<6;++k){
        BOOL r; DWORD e;
        /* cb == 0 */
        n=sizeof ob; SetLastError(SENT); r=a(bin,0,F[k],(char*)ob,&n); e=GetLastError();
        printf("%08lX     %-22s %-6d %-8lu %lu\n",(unsigned long)F[k],"cb=0, buffer given",r,(unsigned long)e,(unsigned long)n);
        /* cb == 0, query */
        n=0; SetLastError(SENT); r=a(bin,0,F[k],NULL,&n); e=GetLastError();
        printf("%08lX     %-22s %-6d %-8lu %lu\n",(unsigned long)F[k],"cb=0, query",r,(unsigned long)e,(unsigned long)n);
        /* a normal success */
        n=sizeof ob; SetLastError(SENT); r=a(bin,16,F[k],(char*)ob,&n); e=GetLastError();
        printf("%08lX     %-22s %-6d %-8lu %lu\n",(unsigned long)F[k],"cb=16, success",r,(unsigned long)e,(unsigned long)n);
        /* a normal query */
        n=0; SetLastError(SENT); r=a(bin,16,F[k],NULL,&n); e=GetLastError();
        printf("%08lX     %-22s %-6d %-8lu %lu\n",(unsigned long)F[k],"cb=16, query",r,(unsigned long)e,(unsigned long)n);
        /* the wide form, cb == 0 */
        n=4096; SetLastError(SENT); r=w(bin,0,F[k],ow,&n); e=GetLastError();
        printf("%08lX     %-22s %-6d %-8lu %lu\n",(unsigned long)F[k],"W cb=0",r,(unsigned long)e,(unsigned long)n);
        printf("\n");
    }
    printf("%u is the sentinel: the export left the caller's value alone.\n", SENT);
    return 0;
}
