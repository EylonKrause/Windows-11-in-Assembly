// changes/118-rtlguidfromstring/correctness.c
// Bit-exact fuzz of wia_guidfromstring vs live ntdll!RtlGUIDFromString + oracle: STATUS + the 16 GUID
// bytes (on success), over valid + malformed GUID strings and heavy random fuzz.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } WIA_USTR;
extern long wia_guidfromstring(const WIA_USTR*, GUID*);
long ref_guidfromstring(const WIA_USTR*, GUID*);
typedef long (WINAPI *fn)(WIA_USTR*, GUID*);
static fn sys;
static int fails=0;

static void chk(wchar_t* s, int lenchars, const char* tag){
    WIA_USTR u; u.Buffer=s; u.Length=(unsigned short)(lenchars*2); u.MaximumLength=(unsigned short)(lenchars*2+2);
    GUID g1,g2,g3; memset(&g1,0x11,sizeof g1); memset(&g2,0x22,sizeof g2); memset(&g3,0x33,sizeof g3);
    long r1=sys(&u,&g1); long r2=wia_guidfromstring(&u,&g2); long r3=ref_guidfromstring(&u,&g3);
    int ok=(r1==r2)&&(r1==r3);
    if(r1==0) ok = ok && !memcmp(&g1,&g2,sizeof g1) && !memcmp(&g1,&g3,sizeof g1);
    if(!ok){ if(fails<25) printf("FAIL [%s] len=%d sys=%lx ours=%lx ref=%lx guidmatch=%d/%d\n",
        tag,lenchars,(unsigned long)r1,(unsigned long)r2,(unsigned long)r3,
        r1==0?!memcmp(&g1,&g2,16):-1,r1==0?!memcmp(&g1,&g3,16):-1); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlGUIDFromString");
    if(!sys){ printf("no RtlGUIDFromString\n"); return 2; }
    // explicit edges (Length = wcslen unless noted)
    struct { wchar_t* s; int len; } E[]={
        {L"{12345678-9abc-def0-1234-56789abcdef0}",38},{L"{00000000-0000-0000-0000-000000000000}",38},
        {L"{FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF}",38},{L"{12345678-9ABC-DEF0-1234-56789ABCDEF0}",38},
        {L"{12345678-9abc-def0-1234-56789abcdefg}",38},{L"(12345678-9abc-def0-1234-56789abcdef0)",38},
        {L"{12345678+9abc-def0-1234-56789abcdef0}",38},{L"{1234567-9abc-def0-1234-56789abcdef0}",37},
        {L"{12345678-9abc-def0-1234-56789abcdef0}",37},{L"{12345678-9abc-def0-1234-56789abcdef0}",39},
        {L"{12345678-9abc-def0-1234-56789abcdef0}extra",38},{L"{12345678-9abc-def0-1234-56789abcde\x0661""0}",38},
        {L"{deadBEEF-cafe-0000-ffff-0123456789ab}",38},
    };
    for(int i=0;i<(int)(sizeof(E)/sizeof(E[0]));i++) chk(E[i].s,E[i].len,"edge");
    // fuzz: build a template then perturb positions/chars
    unsigned long seed=0x118a7011u; wchar_t buf[48];
    static const wchar_t* HX=L"0123456789abcdefABCDEF";
    for(int t=0;t<1500000;t++){
        wcscpy(buf,L"{12345678-9abc-def0-1234-56789abcdef0}");
        seed=seed*1103515245u+12345u; int nper=(seed>>7)%3;   // 0-2 perturbations
        for(int k=0;k<=nper;k++){ seed=seed*1103515245u+12345u; int pos=(seed>>5)%38;
            seed=seed*1103515245u+12345u; int r=(seed>>6)%26;
            wchar_t c = r<22?HX[r] : (r==22?'-':(r==23?'{':(r==24?'}':(wchar_t)0x0661)));
            buf[pos]=c; }
        seed=seed*1103515245u+12345u; int lc = 38 + (int)(((seed>>4)%5)-2);  // len 36..40 sometimes
        if(((seed>>9)&7)!=0) lc=38;
        if(lc<0)lc=0; buf[47]=0;
        chk(buf,lc,"fuzz");
        if(fails>50) break;
    }
    if(!fails) printf("CORRECTNESS: PASS (RtlGUIDFromString vs live + oracle: STATUS + 16 GUID bytes, edges + 1.5M fuzz)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
