#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern size_t wia_strlen(const char*);
size_t ref_strlen(const char*);
typedef size_t (__cdecl *fn)(const char*);
static int failures=0;
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); fn sys=(fn)GetProcAddress(h,"strlen");
    static char buf[512];
    unsigned long seed=0x1234u;
    for(size_t len=0; len<=300; ++len) for(int off=0;off<16;++off){
        char* s=buf+off;
        for(size_t i=0;i<len;i++){ seed=seed*1103515245u+12345u; char c=(char)((seed>>16)|1); s[i]=c?c:1; }
        s[len]=0;
        size_t r=ref_strlen(s), o=wia_strlen(s), y=sys(s);
        if(o!=r||y!=r){ printf("FAIL len=%zu off=%d: ref=%zu ours=%zu sys=%zu\n",len,off,r,o,y); if(++failures>8)return 1; }
    }
    // page-guard
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=1;tail<=64;tail++){ char* s=(char*)(base+pg-tail); for(int i=0;i<tail-1;i++)s[i]='A'; s[tail-1]=0;
        size_t r=ref_strlen(s),o=wia_strlen(s),y=sys(s); if(o!=r||y!=r){printf("FAIL pg tail=%d\n",tail);++failures;} }
    if(!failures) printf("CORRECTNESS: PASS (strlen 0..300 x16 + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
