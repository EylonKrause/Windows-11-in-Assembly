// changes/049-wcslwr/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern wchar_t* wia_wcsupr(wchar_t*);
unsigned short* ref_wcsupr(unsigned short*);
typedef wchar_t* (__cdecl *fn)(wchar_t*);
static int failures=0;
static wchar_t randch(unsigned long* s){
    *s=*s*1103515245u+12345u; unsigned r=*s>>8; int p=r%10;
    if(p<4) return (wchar_t)(0x41+(r%26));
    if(p<7) return (wchar_t)(0x61+(r%26));
    if(p<8) return (wchar_t)(0x20+(r%0x40));
    unsigned nz[]={0xC0,0xE0,0x410,0x430,0x1E9E,0xFF21,0x100,0x17F};
    return (wchar_t)nz[r%8];
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"_wcsupr");
    if(!sys){printf("no _wcsupr\n");return 2;}
    static wchar_t SRC[700], O[700], Y[700], R[700];
    unsigned long seed=0x49abcu;
    for(size_t len=0; len<=320; ++len){
        for(int off=0; off<8; ++off){
            for(size_t i=0;i<700;++i) SRC[i]=0xCCCC;
            wchar_t* s=SRC+off;
            for(size_t i=0;i<len;++i){ wchar_t c=randch(&seed); s[i]=c?c:1; }
            s[len]=0;
            memcpy(O,SRC,sizeof(SRC)); memcpy(Y,SRC,sizeof(SRC)); memcpy(R,SRC,sizeof(SRC));
            wchar_t* ro=wia_wcsupr(O+off);
            wchar_t* ry=sys(Y+off);
            unsigned short* rr=ref_wcsupr((unsigned short*)(R+off));
            int bad=(ro!=O+off)||(ry!=Y+off)||(rr!=(unsigned short*)(R+off));
            if(memcmp(O,R,sizeof(SRC))!=0) bad=1;
            if(memcmp(Y,R,sizeof(SRC))!=0) bad=1;
            if(bad){ printf("FAIL len=%zu off=%d\n",len,off); if(++failures>8) goto endt; }
        }
    }
endt:;
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=2; tail<=200; tail+=2){
        wchar_t* term=(wchar_t*)(base+pg-tail);
        wchar_t* start=(wchar_t*)(base+pg-420);
        for(wchar_t* p=start;p<term;++p){ unsigned long s2=(unsigned long)(size_t)p; *p=(wchar_t)(0x61+((s2>>3)%40)); }
        *term=0;
        static wchar_t rbuf[300]; int n=(int)(term-start);
        memcpy(rbuf,start,(n+1)*2);
        wia_wcsupr(start);
        ref_wcsupr((unsigned short*)rbuf);
        if(memcmp(start,rbuf,(n+1)*2)!=0){ printf("FAIL pg tail=%d\n",tail); if(++failures>8) break; }
    }
    if(!failures) printf("CORRECTNESS: PASS (_wcsupr fuzz 0..320 x8 align: whole-buffer match (no over-write) + non-ascii + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
