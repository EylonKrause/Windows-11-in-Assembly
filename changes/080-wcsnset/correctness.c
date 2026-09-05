// changes/080-wcsnset/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
extern wchar_t* wia_wcsnset(wchar_t*, wchar_t, size_t);
wchar_t* ref_wcsnset(wchar_t*, wchar_t, size_t);
typedef wchar_t* (__cdecl *fn)(wchar_t*, wchar_t, size_t);
static int failures=0;
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); fn sys=(fn)GetProcAddress(h,"_wcsnset");
    if(!sys){printf("no _wcsnset\n");return 2;}
    static wchar_t src[600]; unsigned long seed=0x80abcu;
    static const wchar_t cvals[]={L'X',0,0xFFFF};
    for(size_t len=0; len<=320; ++len){
        for(int off=0; off<8; ++off){
            for(size_t i=0;i<len;i++){ seed=seed*1103515245u+12345u; wchar_t c=(wchar_t)(seed>>13); src[i]=c?c:2; }
            src[len]=0;
            size_t ns[]={0,1,(len>2?len/2:0),len,len+5,len+40};
            for(int ni=0; ni<6; ++ni){ for(int ci=0; ci<3; ++ci){
                wchar_t bo[700],by[700],br[700];
                memset(bo,0xCC,700*2); memset(by,0xCC,700*2); memset(br,0xCC,700*2);
                memcpy(bo+off,src,(len+1)*2); memcpy(by+off,src,(len+1)*2); memcpy(br+off,src,(len+1)*2);
                wchar_t* ro=wia_wcsnset(bo+off,cvals[ci],ns[ni]); wchar_t* ry=sys(by+off,cvals[ci],ns[ni]); wchar_t* rr=ref_wcsnset(br+off,cvals[ci],ns[ni]);
                int bad=(ro!=bo+off)||(ry!=by+off)||(rr!=br+off)||memcmp(bo,by,700*2)||memcmp(bo,br,700*2);
                if(bad){ printf("FAIL len=%zu off=%d c=%d n=%zu\n",len,off,cvals[ci],ns[ni]); if(++failures>8) goto done; }
            }}
        }
    }
done:;
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=1; tail<=160; tail++){
        wchar_t* term=(wchar_t*)(base+pg-tail*2); wchar_t* start=(wchar_t*)(base+pg-320*2);
        int n=(int)(term-start); for(int i=0;i<n;i++) start[i]=(wchar_t)(0x100+((i*7)%0x7000)+1); *term=0;
        static wchar_t rb[400]; memcpy(rb,start,(n+1)*2);
        wia_wcsnset(start,L'Z',400); ref_wcsnset(rb,L'Z',400);
        if(memcmp(start,rb,(n+1)*2)){ printf("FAIL pg tail=%d\n",tail); if(++failures>8) break; }
        memcpy(start,rb,(n+1)*2);
    }
    if(!failures) printf("CORRECTNESS: PASS (_wcsnset fuzz 0..320 x8 align x n{0,1,len/2,len,+5,+40} x c{X,0,FFFF}: whole-buffer + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
