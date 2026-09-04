#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; wchar_t* Buffer; } U;
extern NTSTATUS wia_sidfmt(U*, void*, BOOLEAN);
NTSTATUS ref_sidfmt(U*, const unsigned char*);
typedef NTSTATUS (WINAPI *fn)(U*, void*, BOOLEAN);
static fn sys;
static int failures=0;
static void chk(const unsigned char* sid, USHORT ml){
    wchar_t bo[400],by[400]; for(int i=0;i<400;i++)bo[i]=by[i]=0x2A2A;
    U uo={0,ml,bo}, uy={0,ml,by};
    NTSTATUS ro=wia_sidfmt(&uo,(void*)sid,FALSE); NTSTATUS ry=sys(&uy,(void*)sid,FALSE);
    int bad=(ro!=ry);
    if(ro==0){ if(uo.Length!=uy.Length||wcscmp(bo,by)) bad=1; }
    if(memcmp(bo,by,sizeof(bo))!=0 && ro!=0) bad=1;   // on overflow both untouched
    if(bad){ printf("FAIL ml=%u: ours=%lx/%u '%ls' sys=%lx/%u '%ls'\n",ml,ro,uo.Length,bo,ry,uy.Length,by); ++failures; }
}
void wia_dec2b_init(void);
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlConvertSidToUnicodeString");
    if(!sys){printf("no RtlConvertSidToUnicodeString\n");return 2;}
    unsigned long seed=1;
    for(int t=0;t<3000000 && failures<8;t++){
        unsigned char sid[8+15*4]; seed=seed*1103515245u+12345u; int cnt=(seed>>8)%16;
        sid[0]=1; sid[1]=(unsigned char)cnt;
        for(int i=0;i<6;i++){ seed=seed*1103515245u+12345u; sid[2+i]=(unsigned char)((i<2)?(((seed>>3)&1)?(seed>>16):0):(seed>>16)); }
        for(int i=0;i<cnt;i++){ seed=seed*1103515245u+12345u; unsigned sa=seed; memcpy(sid+8+4*i,&sa,4); }
        chk(sid,600);                                   // ample -> exercises formatting
    }
    // explicit overflow boundary: S-1-5-18 needs Length 16 -> MaximumLength 18
    { unsigned char sid[12]={1,1,0,0,0,0,0,5,18,0,0,0}; for(USHORT ml=8; ml<=22; ml+=2) chk(sid,ml); }
    // rev != 1
    { unsigned char sid[12]={2,1,0,0,0,0,0,5,18,0,0,0}; chk(sid,128); }
    if(!failures) printf("CORRECTNESS: PASS (RtlConvertSidToUnicodeString: 3000000 random SIDs (0..15 subauth, decimal/hex authority) + overflow boundary + rev!=1, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
