// changes/078-strnset/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern char* wia_strnset(char*, int, size_t);
char* ref_strnset(char*, int, size_t);
typedef char* (__cdecl *fn)(char*, int, size_t);
static int failures=0;
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); fn sys=(fn)GetProcAddress(h,"_strnset");
    if(!sys){printf("no _strnset\n");return 2;}
    static char src[600]; unsigned long seed=0x78abcu;
    static const int cs[]={'X',0,0xFF};
    for(size_t len=0; len<=320; ++len){
        for(int off=0; off<8; ++off){
            for(size_t i=0;i<len;i++){ seed=seed*1103515245u+12345u; char c=(char)((seed>>16)|1); src[i]=c?c:2; }
            src[len]=0;
            size_t ns[]={0,1,(len>2?len/2:0),len,len+5,len+40};
            for(int ni=0; ni<6; ++ni){ for(int ci=0; ci<3; ++ci){
                char bo[700],by[700],br[700];
                memset(bo,0xCC,700); memset(by,0xCC,700); memset(br,0xCC,700);
                memcpy(bo+off,src,len+1); memcpy(by+off,src,len+1); memcpy(br+off,src,len+1);
                char* ro=wia_strnset(bo+off,cs[ci],ns[ni]); char* ry=sys(by+off,cs[ci],ns[ni]); char* rr=ref_strnset(br+off,cs[ci],ns[ni]);
                int bad=(ro!=bo+off)||(ry!=by+off)||(rr!=br+off)||memcmp(bo,by,700)||memcmp(bo,br,700);
                if(bad){ printf("FAIL len=%zu off=%d c=%d n=%zu\n",len,off,cs[ci],ns[ni]); if(++failures>8) goto done; }
            }}
        }
    }
done:;
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=1; tail<=200; tail++){
        char* term=(char*)(base+pg-tail); char* start=(char*)(base+pg-320);
        int n=(int)(term-start); for(int i=0;i<n;i++) start[i]=(char)('a'+(i%23)); *term=0;
        static char rb[400]; memcpy(rb,start,n+1);
        wia_strnset(start,'Z',400); ref_strnset(rb,'Z',400);   // n exceeds len -> stops at NUL, must not touch guard page
        if(memcmp(start,rb,n+1)){ printf("FAIL pg tail=%d\n",tail); if(++failures>8) break; }
        memcpy(start,rb,n+1);
    }
    if(!failures) printf("CORRECTNESS: PASS (_strnset fuzz 0..320 x8 align x n{0,1,len/2,len,+5,+40} x c{X,0,FF}: whole-buffer + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
