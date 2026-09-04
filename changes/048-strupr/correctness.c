// changes/047-strlwr/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern char* wia_strupr(char*);
char* ref_strupr(char*);
typedef char* (__cdecl *fn)(char*);
static int failures=0;
static unsigned char randch(unsigned long* s){
    *s=*s*1103515245u+12345u; unsigned r=*s>>8; int p=r%10;
    if(p<4) return (unsigned char)(0x41+(r%26));
    if(p<7) return (unsigned char)(0x61+(r%26));
    if(p<8) return (unsigned char)(0x20+(r%0x40));
    return (unsigned char)(0x80+(r%0x80));
}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll");
    fn sys=(fn)GetProcAddress(h,"_strupr");
    if(!sys){printf("no _strupr\n");return 2;}
    static unsigned char SRC[700], O[700], Y[700], R[700];
    unsigned long seed=0x47abcu;
    for(size_t len=0; len<=320; ++len){
        for(int off=0; off<8; ++off){
            for(size_t i=0;i<sizeof(SRC);++i) SRC[i]=0xCC;               // sentinel around the string
            unsigned char* s=SRC+off;
            for(size_t i=0;i<len;++i){ unsigned char c=randch(&seed); s[i]=c?c:1; }
            s[len]=0;
            // some trailing sentinel bytes after the null must remain 0xCC (no over-write)
            memcpy(O,SRC,sizeof(SRC)); memcpy(Y,SRC,sizeof(SRC)); memcpy(R,SRC,sizeof(SRC));
            char* ro=wia_strupr((char*)(O+off));
            char* ry=sys((char*)(Y+off));
            char* rr=ref_strupr((char*)(R+off));
            int bad=(ro!=(char*)(O+off))||(ry!=(char*)(Y+off))||(rr!=(char*)(R+off));
            if(memcmp(O,R,sizeof(SRC))!=0) bad=1;   // ours must equal reference across the WHOLE buffer
            if(memcmp(Y,R,sizeof(SRC))!=0) bad=1;   // and sys too
            if(bad){ printf("FAIL len=%zu off=%d\n",len,off); if(++failures>8) goto endt; }
        }
    }
endt:;
    // page-guard: string ends right before a NOACCESS page (must not read/write past terminator)
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    for(int tail=1; tail<=140; tail++){
        char* term=(char*)(base+pg-tail);
        char* start=(char*)(base+pg-400);
        for(char* p=start;p<term;++p){ unsigned long s2=(unsigned long)(size_t)p; *p=(char)('A'+((s2>>3)%40)); }
        *term=0;
        static unsigned char rbuf[400];
        int n=(int)(term-start);
        memcpy(rbuf,start,n+1);
        wia_strupr(start);
        ref_strupr((char*)rbuf);
        if(memcmp(start,rbuf,n+1)!=0){ printf("FAIL pg tail=%d\n",tail); if(++failures>8) break; }
    }
    if(!failures) printf("CORRECTNESS: PASS (_strupr fuzz 0..320 x8 align: whole-buffer match (no over-write) + page-guard, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
