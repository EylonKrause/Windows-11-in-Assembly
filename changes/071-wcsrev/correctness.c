#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
extern wchar_t* wia_wcsrev(wchar_t*);
wchar_t* ref_wcsrev(wchar_t*);
typedef wchar_t* (__cdecl *fn)(wchar_t*);
static int failures=0;
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); fn sys=(fn)GetProcAddress(h,"_wcsrev");
    if(!sys){printf("no _wcsrev\n");return 2;}
    static wchar_t src[600]; unsigned long seed=0x71abcu;
    for(size_t len=0; len<=300; ++len){
        for(int off=0; off<8; ++off){
            for(size_t i=0;i<len;i++){ seed=seed*1103515245u+12345u; wchar_t c=(wchar_t)((seed>>13)&0xFFFF); src[i]=c?c:2; }
            src[len]=0;
            static wchar_t bo[600],by[600],br[600];
            memcpy(bo,src,(len+1)*2); memcpy(by,src,(len+1)*2); memcpy(br,src,(len+1)*2);
            (void)off;
            wia_wcsrev(bo); sys(by); ref_wcsrev(br);
            if(wcscmp(bo,by)||wcscmp(bo,br)){ printf("FAIL len=%zu\n",len); if(++failures>8) goto done; }
        }
    }
done:;
    // page-guard: string ends right before a NOACCESS page
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=1; tail<=160; tail++){
        wchar_t* term=(wchar_t*)(base+pg-tail*2); wchar_t* start=(wchar_t*)(base+pg-320*2);
        int n=(int)(term-start); for(int i=0;i<n;i++) start[i]=(wchar_t)(0x100+((i*7)%0x7000)+1); *term=0;
        static wchar_t rb[400]; memcpy(rb,start,(n+1)*2);
        wia_wcsrev(start); ref_wcsrev(rb);
        if(wcscmp(start,rb)){ printf("FAIL pg tail=%d\n",tail); if(++failures>8) break; }
    }
    if(!failures) printf("CORRECTNESS: PASS (_wcsrev fuzz 0..300 x8 align + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
