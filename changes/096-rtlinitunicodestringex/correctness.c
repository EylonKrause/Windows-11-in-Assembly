// changes/096-rtlinitunicodestringex/correctness.c
// Bit-exact vs live ntdll!RtlInitUnicodeStringEx: NTSTATUS + Length/MaximumLength/Buffer,
// across NULL / lengths + the 0x7FFE (STATUS_NAME_TOO_LONG) boundary + a page-guard.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct { USHORT Length; USHORT MaximumLength; PWSTR Buffer; } UNICODE_STRING, *PUNICODE_STRING;
extern LONG wia_rtlinitusex(PUNICODE_STRING, const wchar_t*);
typedef LONG (NTAPI *fn)(PUNICODE_STRING, PCWSTR);
static fn sys;
static int failures=0;
static void chk(const wchar_t* s, const char* what, size_t len){
    UNICODE_STRING a={0xAAAA,0xBBBB,(PWSTR)1}, b={0xCCCC,0xDDDD,(PWSTR)2};
    LONG ra=sys(&a, s);
    LONG rb=wia_rtlinitusex(&b, s);
    if(ra!=rb || a.Length!=b.Length || a.MaximumLength!=b.MaximumLength || a.Buffer!=b.Buffer){
        printf("FAIL [%s] len=%zu: sys{st=%08lx L=%u M=%u B=%p} ours{st=%08lx L=%u M=%u B=%p}\n",
            what,len,ra,a.Length,a.MaximumLength,(void*)a.Buffer,rb,b.Length,b.MaximumLength,(void*)b.Buffer);
        ++failures;
    }
}
int main(void){
    HMODULE h=GetModuleHandleW(L"ntdll.dll");
    sys=(fn)GetProcAddress(h,"RtlInitUnicodeStringEx");
    if(!sys){ printf("no RtlInitUnicodeStringEx\n"); return 2; }
    chk(NULL,"null",0);
    static wchar_t buf[70000];
    for(size_t len=0; len<=600; ++len){
        for(int off=0; off<4; ++off){
            wchar_t* s=buf+off;
            for(size_t i=0;i<len;++i) s[i]=(wchar_t)(L'a'+((i+off)%26));
            s[len]=0;
            chk(s,"rand",len);
        }
    }
    // STATUS_NAME_TOO_LONG boundary: len 0x7FFC..0x8002 wchars
    for(size_t len=0x7FFC; len<=0x8002; ++len){
        for(size_t i=0;i<len;++i) buf[i]=L'x'; buf[len]=0;
        chk(buf,"toolong-boundary",len);
    }
    // page-guard
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=1; tail<=200; ++tail){
        wchar_t* term=(wchar_t*)(base+pg)-tail;
        wchar_t* start=(wchar_t*)(base+pg-400);
        for(wchar_t* p=start;p<term;++p)*p=L'A';
        *term=0;
        chk(start,"pg",tail);
    }
    if(!failures) printf("CORRECTNESS: PASS (RtlInitUnicodeStringEx: null/rand 0..600 x4 + 0x7FFE too-long boundary + page-guard, vs live ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
