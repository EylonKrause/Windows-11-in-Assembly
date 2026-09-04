// changes/051-rtlfindcharinunicodestring/correctness.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } U;
extern NTSTATUS wia_findchar(ULONG, const U*, const U*, USHORT*);
NTSTATUS ref_findchar(unsigned long, const U*, const U*, unsigned short*);
void wia_upcase_init(void);
typedef NTSTATUS (WINAPI *fn)(ULONG, const U*, const U*, USHORT*);
static int failures=0;
static wchar_t randch(unsigned long* s){
    *s=*s*1103515245u+12345u; unsigned r=*s>>8; int p=r%12;
    if(p<3) return (wchar_t)(0x41+(r%26));
    if(p<6) return (wchar_t)(0x61+(r%26));
    if(p<7) return 0;                              // embedded NUL
    if(p<9) return (wchar_t)(0x20+(r%0x50));
    unsigned nz[]={0xC0,0xE0,0x410,0x430,0x1E9E,0xFF21,0x100,0x17F};
    return (wchar_t)nz[r%8];
}
int main(void){
    wia_upcase_init();
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    fn sys=(fn)GetProcAddress(h,"RtlFindCharInUnicodeString");
    if(!sys){printf("no RtlFindCharInUnicodeString\n");return 2;}
    static wchar_t SB[520], CB[40];
    unsigned long seed=0x51abcu;
    for(int t=0;t<40000;t++){
        int slen=t%210, setlen=(t*7+3)%9, soff=t%6;
        wchar_t* sb=SB+soff;
        for(int i=0;i<slen;i++) sb[i]=randch(&seed);
        for(int i=0;i<setlen;i++){
            // sometimes draw set chars from the string so matches actually happen
            seed=seed*1103515245u+12345u;
            if(slen && (seed&1)) CB[i]=sb[(seed>>16)%slen];
            else CB[i]=randch(&seed);
        }
        U us={(USHORT)(slen*2),(USHORT)(slen*2),sb}, cs={(USHORT)(setlen*2),(USHORT)(setlen*2),CB};
        for(ULONG fl=0; fl<8; ++fl){
            USHORT po=0xBBBB, py=0xCCCC, pr=0xDDDD;
            NTSTATUS so=wia_findchar(fl,&us,&cs,&po);
            NTSTATUS sy=sys(fl,&us,&cs,&py);
            NTSTATUS sr=ref_findchar(fl,&us,&cs,&pr);
            int bad=(so!=sy)||(so!=sr)||(po!=py)||(po!=pr);
            if(bad){ printf("FAIL t=%d slen=%d setlen=%d off=%d fl=%lu: ours=%lx/%u sys=%lx/%u ref=%lx/%u\n",
                t,slen,setlen,soff,fl,so,po,sy,py,sr,pr); if(++failures>12) goto endt; }
        }
    }
endt:;
    // page-guard: string ends exactly at a NOACCESS page, find-absent forward -> must not read past Length
    SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
    unsigned char* base=(unsigned char*)VirtualAlloc(NULL,pg*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old; VirtualProtect(base+pg,pg,PAGE_NOACCESS,&old);
    wchar_t setbuf[2]={0x4242,0x4243};
    for(int nb=2; nb<=400; nb+=2){
        wchar_t* sb=(wchar_t*)(base+pg-nb);           // last nb bytes before the guard page
        int nc=nb/2; for(int i=0;i<nc;i++) sb[i]=(wchar_t)(L'a'+(i%23));
        U us={(USHORT)nb,(USHORT)nb,sb}, cs={4,4,setbuf};
        USHORT po=1,py=2; NTSTATUS so=wia_findchar(0,&us,&cs,&po), sy=sys(0,&us,&cs,&py);
        if(so!=sy||po!=py){ printf("FAIL pg nb=%d: ours=%lx/%u sys=%lx/%u\n",nb,so,po,sy,py); if(++failures>12) break; }
        // and a hit at the last char (backward + forward)
        sb[nc-1]=0x4242;
        USHORT p2o,p2y; so=wia_findchar(0,&us,&cs,&p2o); sy=sys(0,&us,&cs,&p2y);
        if(so!=sy||p2o!=p2y){ printf("FAIL pg-hit nb=%d\n",nb); if(++failures>12) break; }
    }
    if(!failures) printf("CORRECTNESS: PASS (RtlFindCharInUnicodeString fuzz 40000 x 8 flags: fwd/bwd/complement/CI + embedded-NUL + non-ascii + page-guard, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
