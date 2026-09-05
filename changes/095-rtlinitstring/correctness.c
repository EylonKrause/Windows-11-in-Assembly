// changes/095-rtlinitstring/correctness.c
// Bit-exact vs live ntdll!RtlInitString (Length, MaximumLength, Buffer): NULL / empty /
// many lengths + the 0xFFFF clamp + a NOACCESS page-guard.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct { USHORT Length; USHORT MaximumLength; PCHAR Buffer; } STRING, *PSTRING;
extern void wia_rtlinitstr(PSTRING, const char*);
typedef VOID (NTAPI *fn)(PSTRING, const char*);
static fn sys;
static int failures=0;
static void chk(const char* s, const char* what, size_t len){
    STRING a={0xAAAA,0xBBBB,(PCHAR)1}, b={0xCCCC,0xDDDD,(PCHAR)2};
    sys(&a, s);
    wia_rtlinitstr(&b, s);
    if(a.Length!=b.Length || a.MaximumLength!=b.MaximumLength || a.Buffer!=b.Buffer){
        printf("FAIL [%s] len=%zu: sys{L=%u M=%u B=%p} ours{L=%u M=%u B=%p}\n",
            what,len,a.Length,a.MaximumLength,(void*)a.Buffer,b.Length,b.MaximumLength,(void*)b.Buffer);
        ++failures;
    }
}
int main(void){
    HMODULE h=GetModuleHandleW(L"ntdll.dll");
    sys=(fn)GetProcAddress(h,"RtlInitString");
    if(!sys){ printf("no RtlInitString\n"); return 2; }
    chk(NULL,"null",0);
    static char buf[140000];
    for(size_t len=0; len<=800; ++len){
        for(int off=0; off<4; ++off){
            char* s=buf+off;
            for(size_t i=0;i<len;++i) s[i]=(char)('a'+((i+off)%26));
            s[len]=0;
            chk(s,"rand",len);
        }
    }
    // 0xFFFF clamp boundary
    for(size_t len=65530; len<=65540; ++len){
        for(size_t i=0;i<len;++i) buf[i]='x'; buf[len]=0;
        chk(buf,"clamp",len);
    }
    for(size_t i=0;i<130000;++i) buf[i]='q'; buf[130000]=0;
    chk(buf,"huge",130000);
    // page-guard
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=1; tail<=200; ++tail){
        char* term=(char*)(base+pg-tail);
        char* start=(char*)(base+pg-400);
        for(char* p=start;p<term;++p)*p='A';
        *term=0;
        chk(start,"pg",tail);
    }
    if(!failures) printf("CORRECTNESS: PASS (RtlInitString: null/rand 0..800 x4 off + 0xFFFF clamp + huge + page-guard, vs live ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
