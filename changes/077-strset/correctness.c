// changes/077-strset/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern char* wia_strset(char*, int);
char* ref_strset(char*, int);
typedef char* (__cdecl *fn)(char*, int);
static int failures=0;
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); fn sys=(fn)GetProcAddress(h,"_strset");
    if(!sys){printf("no _strset\n");return 2;}
    static char src[600]; unsigned long seed=0x77abcu;
    static const int cs[]={'X',0,1,0xFF,0x41,0x80};
    for(size_t len=0; len<=320; ++len){
        for(int off=0; off<8; ++off){
            for(size_t i=0;i<len;i++){ seed=seed*1103515245u+12345u; char c=(char)((seed>>16)|1); src[i]=c?c:2; }
            src[len]=0;
            for(int ci=0; ci<6; ++ci){
                char bo[640],by[640],br[640];
                memset(bo,0xCC,640); memset(by,0xCC,640); memset(br,0xCC,640);
                memcpy(bo+off,src,len+1); memcpy(by+off,src,len+1); memcpy(br+off,src,len+1);
                char* ro=wia_strset(bo+off,cs[ci]); char* ry=sys(by+off,cs[ci]); char* rr=ref_strset(br+off,cs[ci]);
                int bad=(ro!=bo+off)||(ry!=by+off)||(rr!=br+off)||memcmp(bo,by,640)||memcmp(bo,br,640);
                if(bad){ printf("FAIL len=%zu off=%d c=%d\n",len,off,cs[ci]); if(++failures>8) goto done; }
            }
        }
    }
done:;
    // page-guard: string ends right before a NOACCESS page (over-read/write would fault)
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=1; tail<=200; tail++){
        char* term=(char*)(base+pg-tail); char* start=(char*)(base+pg-320);
        int n=(int)(term-start); for(int i=0;i<n;i++) start[i]=(char)('a'+(i%23)); *term=0;
        static char rb[400]; memcpy(rb,start,n+1);
        wia_strset(start,'Z'); ref_strset(rb,'Z');
        if(memcmp(start,rb,n+1)){ printf("FAIL pg tail=%d\n",tail); if(++failures>8) break; }
        memcpy(start,rb,n+1);
    }
    if(!failures) printf("CORRECTNESS: PASS (_strset fuzz 0..320 x8 align x c{X,0,1,FF,A,80}: whole-buffer (no over-write) + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
