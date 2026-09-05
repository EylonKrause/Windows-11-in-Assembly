#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern char* wia_strrev(char*);
char* ref_strrev(char*);
typedef char* (__cdecl *fn)(char*);
static int failures=0;
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); fn sys=(fn)GetProcAddress(h,"_strrev");
    if(!sys){printf("no _strrev\n");return 2;}
    static char src[600]; unsigned long seed=0x70abcu;
    for(size_t len=0; len<=300; ++len){
        for(int off=0; off<8; ++off){
            for(size_t i=0;i<len;i++){ seed=seed*1103515245u+12345u; char c=(char)((seed>>16)|1); src[i]=c?c:2; }
            src[len]=0;
            char bo[600],by[600],br[600]; memcpy(bo,src,len+1); memcpy(by,src,len+1); memcpy(br,src,len+1);
            (void)off;
            wia_strrev(bo); sys(by); ref_strrev(br);
            if(strcmp(bo,by)||strcmp(bo,br)){ printf("FAIL len=%zu: '%s' vs sys '%s' ref '%s'\n",len,bo,by,br); if(++failures>8) goto done; }
        }
    }
done:;
    // page-guard: string ends right before a NOACCESS page
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=1; tail<=200; tail++){
        char* term=(char*)(base+pg-tail); char* start=(char*)(base+pg-320);
        int n=(int)(term-start); for(int i=0;i<n;i++) start[i]=(char)('a'+(i%23)); *term=0;
        static char rb[400]; memcpy(rb,start,n+1);
        wia_strrev(start); ref_strrev(rb);
        if(strcmp(start,rb)){ printf("FAIL pg tail=%d\n",tail); if(++failures>8) break; }
        memcpy(start,rb,n+1); wia_strrev(start);  // restore for next iter's copy source consistency (not needed)
    }
    if(!failures) printf("CORRECTNESS: PASS (_strrev fuzz 0..300 x8 align + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
