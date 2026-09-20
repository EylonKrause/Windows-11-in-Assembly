// changes/018-rtlunicodestringtoansistring/correctness.c
//
// Bit-exact fuzz of wia_u2a vs live ntdll!RtlUnicodeStringToAnsiString + oracle.
//
// REWRITTEN 2026-09-20. The previous version could not see either of the two rules this change
// turned out to be missing, and it is worth being precise about why, because the shape of the
// mistake is more useful than the mistake:
//
//   * it ran every case with `MaximumLength` fixed at 300 -- always generous -- so the size rule
//     never bound and the OVERFLOW path was never compared against ntdll at all (the one overflow
//     assertion it had checked only that OUR function returned 0x80000005, with no live call and
//     no buffer comparison);
//   * it compared indices 0..Length-1 only, so the NUL the export writes at [Length] was outside
//     the comparison by construction.
//
// Both are now the opposite: `MaximumLength` is swept across every interesting value for every
// length, and the whole destination buffer is compared byte for byte against both the live export
// and the oracle, poison included.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef LONG NTSTATUS;
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } USTR;
typedef struct { unsigned short Length, MaximumLength; char* Buffer; } ASTR;
extern NTSTATUS wia_u2a(ASTR*, const USTR*, BOOLEAN);
long ref_u2a(void*, const void*, int);
void wia_ansimap_init(void);
typedef NTSTATUS (WINAPI *fn)(ASTR*,const USTR*,BOOLEAN);
static fn sys;
static int failures=0;

#define DB 400
#define POISON 0x71

static void one(const wchar_t* src, int n, unsigned short maxlen)
{
    static char d1[DB], d2[DB], dr[DB];
    USTR us; ASTR u1,u2,ur;
    long yo,oo,ro; int bad=0, i;
    if(failures>8) return;
    memset(d1,POISON,DB); memset(d2,POISON,DB); memset(dr,POISON,DB);
    us.Length=(unsigned short)(n*2); us.MaximumLength=(unsigned short)(n*2); us.Buffer=(wchar_t*)src;
    u1.Length=0x5A5A; u1.MaximumLength=maxlen; u1.Buffer=d1;
    u2.Length=0x5A5A; u2.MaximumLength=maxlen; u2.Buffer=d2;
    ur.Length=0x5A5A; ur.MaximumLength=maxlen; ur.Buffer=dr;
    yo=sys(&u1,&us,FALSE);
    oo=wia_u2a(&u2,&us,0);
    ro=ref_u2a((void*)&ur,(void*)&us,0);
    if(yo!=oo||yo!=ro) bad=1;
    if(u1.Length!=u2.Length||u1.Length!=ur.Length) bad=1;
    if(u1.MaximumLength!=u2.MaximumLength||u1.MaximumLength!=ur.MaximumLength) bad=1;
    /* the whole buffer, so the terminator and the truncating write are both in scope */
    if(memcmp(d1,d2,DB)||memcmp(d1,dr,DB)) bad=1;
    if(bad){
        printf("FAIL n=%d max=%u: ntdll st=%08lX len=%u | ours st=%08lX len=%u | ref st=%08lX len=%u\n",
               n,maxlen,yo,u1.Length,oo,u2.Length,ro,ur.Length);
        for(i=0;i<DB;i++)
            if(d1[i]!=d2[i]){ printf("  first byte diff at %d: ntdll=%02X ours=%02X ref=%02X\n",
                                     i,(unsigned char)d1[i],(unsigned char)d2[i],(unsigned char)dr[i]); break; }
        ++failures;
    }
}

int main(void){
    static wchar_t src[300];
    unsigned long seed=1;
    int n,i,k;
    wia_ansimap_init();
    { HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlUnicodeStringToAnsiString"); }
    if(!sys){ printf("no RtlUnicodeStringToAnsiString\n"); return 2; }

    for(n=0;n<=280 && failures<=8;++n){
        int rng=(n%4)?0x80:0x600;
        for(i=0;i<n;i++){ seed=seed*1103515245u+12345u; src[i]=(wchar_t)((seed>>16)%rng+1); }

        /* generous, as the old gate always was */
        one(src,n,300);

        /* and the whole interesting neighbourhood of the boundary: the export needs n+1 bytes,
           so n-1, n, n+1 and n+2 straddle it, and 0 and 1 are the degenerate ends */
        one(src,n,0);
        one(src,n,1);
        if(n>0) one(src,n,(unsigned short)(n-1));
        one(src,n,(unsigned short)n);
        one(src,n,(unsigned short)(n+1));
        one(src,n,(unsigned short)(n+2));
        /* deep truncation, where the partial write is most of the buffer */
        if(n>8){ one(src,n,(unsigned short)(n/2)); one(src,n,(unsigned short)(n/4)); }
    }

    /* every MaximumLength from 0 to 40 against a fixed 32-character source, so the truncation
       length is swept one byte at a time through the 16-element block boundary */
    for(i=0;i<32;i++) src[i]=(wchar_t)(L'A'+(i%26));
    for(k=0;k<=40 && failures<=8;++k) one(src,32,(unsigned short)k);
    /* the same with high characters, which take the table path rather than the packed path */
    for(i=0;i<32;i++) src[i]=(wchar_t)(0x00C0+(i%32));
    for(k=0;k<=40 && failures<=8;++k) one(src,32,(unsigned short)k);

    if(!failures)
        printf("CORRECTNESS: PASS (UTF-16->ANSI n=0..280 ASCII+nonASCII, MaximumLength swept across\n"
               "  the n+1 boundary and 0..40 against a 32-character source on both the packed and the\n"
               "  table path, WHOLE destination buffer compared so the NUL terminator and the\n"
               "  truncating partial write on overflow are both in scope, vs ntdll + oracle)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
