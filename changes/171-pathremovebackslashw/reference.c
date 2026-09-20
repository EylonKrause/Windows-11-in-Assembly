// changes/171-pathremovebackslashw/reference.c
// The correctness oracle: the obvious scalar PathRemoveBackslashW. Not fast; just correct.
// Contract derived in probes/prb.c and fuzz-confirmed against the live export
// (2,000,000 cases, 0 mismatches):
//   The return is always psz + max(n-1,0), a pointer to the last character, not the
//   terminator. One trailing backslash is removed unless the RESULT would be a bare root:
//       m==0, or (m==1 && psz[0]=='\'), or (m==2 && psz[1]==':' && drive_letter(psz[0]))
//   A forward slash is NOT a separator here.
// The drive-letter set was pinned by an exhaustive 65535-character sweep: exactly the ASCII
// letters plus the Latin-1 letters (U+00D7 and U+00F7 excluded; nothing at or above U+0100).
#include <wchar.h>

static int drive_letter(wchar_t c){
    return (c >= 0x41 && c <= 0x5A) || (c >= 0x61 && c <= 0x7A)
        || (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) || (c >= 0xF8 && c <= 0xFF);
}

wchar_t* ref_pathremovebackslashw(wchar_t* psz){
    int n = 0;
    while(psz[n]) ++n;
    if(n == 0) return psz;
    wchar_t* ret = psz + n - 1;
    if(psz[n-1] == L'\\'){
        int m = n - 1;
        int prot = (m == 0)
                || (m == 1 && psz[0] == L'\\')
                || (m == 2 && psz[1] == L':' && drive_letter(psz[0]));
        if(!prot) psz[n-1] = 0;
    }
    return ret;
}
