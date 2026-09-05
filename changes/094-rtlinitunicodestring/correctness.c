// changes/094-rtlinitunicodestring/correctness.c
// Bit-exact vs live ntdll!RtlInitUnicodeString (Length, MaximumLength, Buffer) across
// NULL / empty / many lengths + the 0xFFFE overflow clamp + a NOACCESS page-guard.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct { USHORT Length; USHORT MaximumLength; PWSTR Buffer; } UNICODE_STRING, *PUNICODE_STRING;
extern void wia_rtlinitus(PUNICODE_STRING, const wchar_t*);
typedef VOID (NTAPI *fn)(PUNICODE_STRING, PCWSTR);
static fn sys;
static int failures=0;
static void chk(const wchar_t* s, const char* what, size_t len){
    UNICODE_STRING a={0xAAAA,0xBBBB,(PWSTR)1}, b={0xCCCC,0xDDDD,(PWSTR)2};
    sys(&a, s);
    wia_rtlinitus(&b, s);
    if(a.Length!=b.Length || a.MaximumLength!=b.MaximumLength || a.Buffer!=b.Buffer){
        printf("FAIL [%s] len=%zu: sys{L=%u M=%u B=%p} ours{L=%u M=%u B=%p}\n",
            what,len,a.Length,a.MaximumLength,(void*)a.Buffer,b.Length,b.MaximumLength,(void*)b.Buffer);
        ++failures;
    }
}
int main(void){
    HMODULE h=GetModuleHandleW(L"ntdll.dll");
    sys=(fn)GetProcAddress(h,"RtlInitUnicodeString");
    if(!sys){ printf("no RtlInitUnicodeString\n"); return 2; }
    chk(NULL,"null",0);
    static wchar_t buf[70000];
    for(size_t len=0; len<=600; ++len){
        for(size_t i=0;i<len;++i) buf[i]=(wchar_t)(L'a'+(i%26));
        buf[len]=0;
        // test at a few sub-alignments too
        for(int off=0; off<4; ++off){
            wchar_t* s=buf+off; if(len) s[len-1]=s[len-1]; // no-op; keep buffer valid
            // rebuild with offset start
            for(size_t i=0;i<len;++i) s[i]=(wchar_t)(L'a'+((i+off)%26));
            s[len]=0;
            chk(s,"rand",len);
        }
    }
    // 0xFFFE clamp boundary: lengths around 32766..32770 wchars
    for(size_t len=32760; len<=32772; ++len){
        for(size_t i=0;i<len;++i) buf[i]=L'x';
        buf[len]=0;
        chk(buf,"clamp",len);
    }
    // huge
    for(size_t i=0;i<69000;++i) buf[i]=L'q'; buf[69000]=0;
    chk(buf,"huge",69000);
    // page-guard: wide string ends right before a NOACCESS page
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
    if(!failures) printf("CORRECTNESS: PASS (RtlInitUnicodeString: null/rand 0..600 x4 off + 0xFFFE clamp + huge + page-guard, vs live ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
