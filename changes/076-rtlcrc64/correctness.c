// changes/076-rtlcrc64/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
extern unsigned long long wia_crc64(const void*, size_t, unsigned long long);
unsigned long long ref_crc64(const void*, size_t, unsigned long long);
void wia_crc64_init(void);
typedef unsigned long long (WINAPI *fn)(const void*, size_t, unsigned long long);
int main(void){
    wia_crc64_init();
    HMODULE h=GetModuleHandleW(L"ntdll.dll");
    fn sys=(fn)GetProcAddress(h,"RtlCrc64");
    if(!sys){printf("no RtlCrc64\n");return 2;}
    unsigned long seed=0x76abcu; int fails=0; static unsigned char buf[4096];
    for(int t=0;t<400000;t++){
        int n=t%2000;
        for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; buf[i]=(unsigned char)(seed>>16); }
        seed=seed*1103515245u+12345u; unsigned long long init=((unsigned long long)seed<<32)^(seed*2654435761u);
        if((t&3)==0)init=0; if((t&3)==1)init=~0ull;
        unsigned long long a=wia_crc64(buf,n,init), s=sys(buf,n,init), r=ref_crc64(buf,n,init);
        if(a!=s||a!=r){ if(fails<6) printf("FAIL n=%d init=%016llX ours=%016llX sys=%016llX ref=%016llX\n",n,init,a,s,r); if(++fails>10) break; }
    }
    // page-guard: buffer ends right before a NOACCESS page (over-read would fault)
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    for(int len=0;len<=200;len++){
        unsigned char* s=base+pg-len; for(int i=0;i<len;i++) s[i]=(unsigned char)(i*13+7);
        unsigned long long a=wia_crc64(s,len,0), r=ref_crc64(s,len,0);
        if(a!=r){ printf("FAIL pg len=%d\n",len); if(++fails>10) break; }
    }
    if(!fails) printf("CORRECTNESS: PASS (RtlCrc64 fuzz 400000 x lengths 0..1999 x inits {0,~0,rand} + page-guard, vs live ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
