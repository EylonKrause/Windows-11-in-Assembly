#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern size_t wia_cmpmemulong(const void*, size_t, unsigned long);
size_t ref_cmpmemulong(const void*, size_t, unsigned long);
typedef SIZE_T (WINAPI *fn)(const void*, SIZE_T, ULONG);
static int failures=0;
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); fn sys=(fn)GetProcAddress(h,"RtlCompareMemoryUlong");
    static unsigned long buf[600]; unsigned long seed=1, pat=0xA5A5A5A5;
    for(int words=0; words<=400 && failures<8; ++words){
        for(int i=0;i<words;i++) buf[i]=pat;
        SIZE_T len=words*4;
        // all match
        size_t y=sys(buf,len,pat), o=wia_cmpmemulong(buf,len,pat), r=ref_cmpmemulong(buf,len,pat);
        if(y!=o||o!=r){ printf("FAIL match words=%d: ntdll=%zu ours=%zu ref=%zu\n",words,y,o,r); if(++failures>8)return 1; }
        // mismatch at various positions
        for(int pos=0; pos<words; pos += (words>20?7:1)){
            buf[pos]=~pat;
            y=sys(buf,len,pat); o=wia_cmpmemulong(buf,len,pat); r=ref_cmpmemulong(buf,len,pat);
            if(y!=o||o!=r){ printf("FAIL diff words=%d pos=%d: ntdll=%zu ours=%zu ref=%zu\n",words,pos,y,o,r); if(++failures>8)return 1; }
            buf[pos]=pat;
        }
    }
    if(!failures) printf("CORRECTNESS: PASS (pattern compare words=0..400, match + diff at every pos, vs ntdll)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
