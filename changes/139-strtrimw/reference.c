// changes/139-strtrimw/reference.c
// Oracle for shlwapi!StrTrimW. In-place trim of leading and trailing set members; returns TRUE if
// anything changed. The order matters for the bytes left past the new terminator: the trailing NUL is
// written BEFORE the remainder is shifted down (probed against the live export).
#include <wchar.h>
static int in_set(wchar_t c, const wchar_t* set){
    for(; *set; ++set) if(*set==c) return 1;
    return 0;
}
int ref_strtrimw(wchar_t* psz, const wchar_t* set){
    if(set[0]==0) return 0;
    long lead=0;
    while(psz[lead] && in_set(psz[lead],set)) lead++;
    if(psz[lead]==0){                      // entirely trim characters (or empty)
        if(lead==0) return 0;
        psz[0]=0; return 1;
    }
    long len=0; while(psz[len]) len++;
    long j=len;
    while(j>lead && in_set(psz[j-1],set)) j--;
    int changed=0;
    if(j!=len){ psz[j]=0; changed=1; }
    if(lead>0){
        changed=1;
        long n=j-lead+1;                   // including the terminator
        for(long i=0;i<n;i++) psz[i]=psz[lead+i];
    }
    return changed;
}
