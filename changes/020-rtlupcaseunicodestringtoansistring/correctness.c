// changes/020-rtlupcaseunicodestringtoansistring/correctness.c
//
// Bit-exact fuzz of wia_u2au vs live ntdll!RtlUpcaseUnicodeStringToAnsiString + oracle.
//
// REWRITTEN 2026-09-20. The previous version could not see either of the two rules this change
// turned out to be missing, and the shape of that blindness is more useful than the bug itself:
//
//   * every case ran with MaximumLength fixed and generous, so the size rule never bound and the
//     OVERFLOW path was never compared against ntdll at all -- the single overflow assertion it
//     had checked only that OUR function returned 0x80000005, with no live call and no buffer
//     comparison;
//   * the comparison stopped at Length, so the terminator the export writes at [Length] was
//     outside it by construction.
//
// Now MaximumLength is swept across the whole interesting neighbourhood for every length, and the
// whole destination buffer is compared byte for byte against both the live export and the oracle,
// poison included. The export needs n+1 bytes; on overflow it writes nothing and leaves Length as
// the caller had it.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef LONG NTSTATUS;
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } USTR;
typedef struct { unsigned short Length, MaximumLength; char* Buffer; } ASTR;
extern NTSTATUS wia_u2au(ASTR*, const USTR*, unsigned char);
long ref_u2au(void*, const void*, int);
void wia_upansimap_init(void);
typedef NTSTATUS (WINAPI *fn)(ASTR*, const USTR*, BOOLEAN);
static fn sys;
static int failures=0;

#define DBYTES 900
#define POISON 0x71

static void one(const wchar_t* src, int n, unsigned short maxlen)
{
    static char d1[DBYTES], d2[DBYTES], dr[DBYTES];
    USTR us; ASTR u1,u2,ur;
    long yo,oo,ro; int bad=0, i;
    if(failures>8) return;
    memset(d1,POISON,DBYTES); memset(d2,POISON,DBYTES); memset(dr,POISON,DBYTES);
    us.Length=(unsigned short)(n*2); us.MaximumLength=us.Length; us.Buffer=(wchar_t*)src;
    u1.Length=0x5A5A; u1.MaximumLength=maxlen; u1.Buffer=(char*)d1;
    u2.Length=0x5A5A; u2.MaximumLength=maxlen; u2.Buffer=(char*)d2;
    ur.Length=0x5A5A; ur.MaximumLength=maxlen; ur.Buffer=(char*)dr;
    yo=sys(&u1,&us,FALSE);
    oo=wia_u2au(&u2,&us,0);
    ro=ref_u2au((void*)&ur,(void*)&us,0);
    if(yo!=oo||yo!=ro) bad=1;
    if(u1.Length!=u2.Length||u1.Length!=ur.Length) bad=1;
    if(u1.MaximumLength!=u2.MaximumLength||u1.MaximumLength!=ur.MaximumLength) bad=1;
    if(memcmp(d1,d2,DBYTES)||memcmp(d1,dr,DBYTES)) bad=1;   /* the WHOLE buffer */
    if(bad){
        printf("FAIL n=%d max=%u: ntdll st=%08lX len=%u | ours st=%08lX len=%u | ref st=%08lX len=%u\n",
               n,maxlen,yo,u1.Length,oo,u2.Length,ro,ur.Length);
        for(i=0;i<DBYTES;i++)
            if(d1[i]!=d2[i]){ printf("  first byte diff at %d: ntdll=%02X ours=%02X ref=%02X\n",
                                     i,(unsigned char)d1[i],(unsigned char)d2[i],(unsigned char)dr[i]); break; }
        ++failures;
    }
}

int main(void){
    static wchar_t src[300];
    unsigned long seed=1;
    int n,i,k,need;
    wia_upansimap_init();
    { HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlUpcaseUnicodeStringToAnsiString"); }
    if(!sys){ printf("no RtlUpcaseUnicodeStringToAnsiString\n"); return 2; }

    for(n=0;n<=280 && failures<=8;++n){
        int rng=(n%3)?0x80:0x600;
        for(i=0;i<n;i++){ seed=seed*1103515245u+12345u; src[i]=(wchar_t)((seed>>16)%rng+1); }
        need = n + 1;
        one(src,n,800);                       /* generous, as the old gate always was */
        one(src,n,0);
        one(src,n,1);
        one(src,n,(unsigned short)(need-2 > 0 ? need-2 : 0));
        one(src,n,(unsigned short)(need-1));
        one(src,n,(unsigned short)need);       /* exactly enough, including the terminator */
        one(src,n,(unsigned short)(need+1));
        if(n>8) one(src,n,(unsigned short)(need/2));
    }

    /* every MaximumLength from 0 to 80 against a fixed 32-element source, so the boundary is
       swept one byte at a time -- including the ODD values, which matter for the wide direction */
    for(i=0;i<32;i++) src[i]=(wchar_t)(L'A'+(i%26));
    for(k=0;k<=80 && failures<=8;++k) one(src,32,(unsigned short)k);
    /* and again with high elements, which take the table path rather than the in-register path */
    for(i=0;i<32;i++) src[i]=(wchar_t)(0x00C0+(i%32));
    for(k=0;k<=80 && failures<=8;++k) one(src,32,(unsigned short)k);

    if(!failures)
        printf("CORRECTNESS: PASS (RtlUpcaseUnicodeStringToAnsiString n=0..280, MaximumLength swept across the\n"
               "  n+1 bytes boundary and 0..80 against a 32-element source on both the in-register\n"
               "  and the table path, WHOLE destination buffer compared so the NUL terminator and\n"
               "  the untouched-on-overflow rule are both in scope, vs ntdll + oracle)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
