// changes/141-pathremoveblanksw/reference.c
// Oracle for shlwapi!PathRemoveBlanksW. Only SPACE is stripped (not tab), there is no MAX_PATH guard,
// and the order is MOVE FIRST then terminate; the reverse of StrTrimW (change 139). That ordering is
// observable in the bytes past the new terminator, so it is reproduced rather than approximated.
#include <wchar.h>
void ref_pathremoveblanksw(wchar_t* p){
    long lead=0; while(p[lead]==L' ') lead++;
    long len=0; while(p[len]) len++;
    if(lead>0){
        long n=len-lead+1;                 // including the terminator
        for(long i=0;i<n;i++) p[i]=p[lead+i];
        len-=lead;
    }
    long j=len;
    while(j>0 && p[j-1]==L' ') j--;
    if(j!=len) p[j]=0;
}
