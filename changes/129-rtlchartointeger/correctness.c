// changes/129-rtlchartointeger/correctness.c
// Bit-exact fuzz of wia_char2int vs live ntdll!RtlCharToInteger + oracle: NTSTATUS AND *Value (checked
// for non-writing on invalid base), over explicit edges, every byte value in leading and embedded
// position across 5 bases, and 3M random fuzz.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern long wia_char2int(const char*, unsigned long, unsigned long*);
long ref_char2int(const char*, unsigned long, unsigned long*);
typedef long (WINAPI *fn)(const char*, unsigned long, unsigned long*);
static fn sys;
static int fails=0;
static void chk(const char* s, unsigned long base){
    unsigned long a=0xDEADBEEF,b=0xDEADBEEF,c=0xDEADBEEF;
    long r1=sys(s,base,&a), r2=wia_char2int(s,base,&b), r3=ref_char2int(s,base,&c);
    if(r1!=r2||r1!=r3||a!=b||a!=c){ if(fails<25) printf("FAIL [%s] f=%02X base=%lu sys{%08lX,%lu} ours{%08lX,%lu} ref{%08lX,%lu}\n",
        s,(unsigned char)s[0],base,r1,a,r2,b,r3,c); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlCharToInteger");
    if(!sys){ printf("no RtlCharToInteger\n"); return 2; }
    const char* S[]={"1234","0x1F","1F","777","0777","0b101","0o17","  42","\t42","+42","-42","","abc",
        "12abc","4294967295","4294967296","99999999999999999999","7","8","z","1","2"," 0x10","0X10","0",
        "00","0x","-0x10","+0x10","-","+","0b","0o","0xg","0b2","0o8"," \t\n\v\f\r 9","--5","+-5","- 42",
        "0x7FFFFFFF","0xFFFFFFFF","0x100000000","-0","2147483648","0xabcdef","0xABCDEF","0Xff","0B11","0O7"};
    /* The last five entries are not padding. impl.asm validates a caller-supplied base with a range
       test followed by `bt r10d, edx`, and BT TAKES ITS BIT INDEX MODULO 32 -- so a base of 0x80000002
       indexes bit 2, which is a SET bit in the mask, and would be accepted as base 2 if the range test
       ever let it through. The range test is unsigned (`ja`) and does reject it, but mutation mutant #22
       changed that one instruction to a SIGNED compare (`jg`), which lets every base >= 0x80000000
       through, and THE MUTANT SURVIVED both gate 1 and gate 4: the sweep's largest base was
       0xFFFFFFFF, whose low five bits are 31, a clear bit, so it was refused either way and the hole
       was invisible. These five alias onto bits 2, 8, 16 and 0. */
    unsigned long BASES[]={0,2,8,10,16,1,3,4,7,9,15,17,32,36,255,0xFFFFFFFF,
                           0x80000002,0x80000008,0x80000010,0x80000000,0xFFFFFFE2};
    const int NBASES = (int)(sizeof(BASES)/sizeof(BASES[0]));
    for(int i=0;i<(int)(sizeof(S)/sizeof(S[0]));i++)
        for(int b=0;b<(int)(sizeof(BASES)/sizeof(BASES[0]));b++) chk(S[i],BASES[b]);
    char one[2]={0,0};
    for(int c=1;c<256 && fails<25;c++){ one[0]=(char)c; for(int b=0;b<5;b++) chk(one,BASES[b]); }
    char buf[32];
    for(int c=1;c<256 && fails<25;c++){ sprintf(buf,"%c42",(char)c);   for(int b=0;b<5;b++) chk(buf,BASES[b]); }
    for(int c=1;c<256 && fails<25;c++){ sprintf(buf,"1%c2",(char)c);   for(int b=0;b<5;b++) chk(buf,BASES[b]); }
    for(int c=1;c<256 && fails<25;c++){ sprintf(buf,"%c0x1f",(char)c); chk(buf,0); }
    unsigned long seed=0xc2117u; char f[24];
    for(int t=0;t<3000000 && fails<25;t++){
        seed=seed*1103515245u+12345u; int n=1+((seed>>7)%12); int j=0;
        for(int k=0;k<n;k++){ seed=seed*1103515245u+12345u; int r=(seed>>8)%22;
            f[j++]= r<10?('0'+r) : r<16?('a'+r-10) : r==16?'x' : r==17?'b' : r==18?'o' : r==19?'-' : r==20?'+':' '; }
        f[j]=0;
        /* %NBASES, not %16: the count was hard-coded, so the five aliasing bases appended above
           would have been added to the array and never drawn. A corpus that cannot reach its own
           new cases is the same defect in miniature. */
        seed=seed*1103515245u+12345u; chk(f,BASES[(seed>>5)%NBASES]);
    }
    /* ---- PLANTED BYTES: the cases a corpus of string LITERALS cannot express -------------------
       Everything above this point passes a literal, so whatever follows a terminator is whatever the
       linker put there. That is why the leading-NUL rule went unnoticed: "" and "abc" happened to give
       0 for all sixteen bases, which is what the implementation returned, and the two were
       indistinguishable. Here the bytes after the NUL are CHOSEN, so the question can be asked.

       The pattern is built in a zeroed buffer and every arrangement from probes/pastnul2.c is driven,
       plus a sweep of one planted byte at every position for good measure. */
    {
        static char pb[64];
        static const unsigned char pats[][8] = {
            { 0x00, '6', 0 },                  { 0x00, '4', '2', 0 },
            { 0x00, 0x20, '6', 0 },            { 0x00, 0x09, '6', 0 },
            { 0x00, 0x80, '6', 0 },            { 0x00, 0x00, '6', 0 },
            { 0x00, '-', '6', 0 },             { 0x00, '+', '6', 0 },
            { 0x20, 0x00, '8', 0 },            { 0x09, 0x00, '8', 0 },
            { 0x80, 0x00, '8', 0 },            { 0x20, 0x20, 0x00, '8', 0 },
            { 0x20, 0x00, 0x00, '8', 0 },      { '-', 0x00, '9', 0 },
            { 0x20, '-', 0x00, '9', 0 },       { 0x00, '-', '9', 0 },
            { '1', 0x00, '2', 0 },             { 0x00, '1', 0x00, '2', 0 },
            { 0x00, '0', 'x', '1', 'f', 0 },   { 0x00, '0', '7', '7', '7', 0 },
            { 0x00, '0', 'X', '1', 'f', 0 },   { 0x00, '0', 'b', '1', '1', 0 },
            { 0x00, '0', 'o', '7', '7', 0 },   { 0x00, 0x00, 0x00, 0x00, 0 },
            { 0x00, 'z', 0 },                  { 0x00, 0xFF, '5', 0 },
            { 0xFF, 0x00, '5', 0 },            { 0x00, '9', 0 },
        };
        int pi, pj, pk;
        for(pi=0; pi<(int)(sizeof(pats)/sizeof(pats[0])) && fails<25; ++pi){
            for(pj=0; pj<64; ++pj) pb[pj]=0;
            for(pj=0; pj<8; ++pj) pb[pj]=(char)pats[pi][pj];
            for(pk=0; pk<(int)(sizeof(BASES)/sizeof(BASES[0])); ++pk) chk(pb, BASES[pk]);
        }
        /* one planted digit at every position behind a leading NUL, so "how far does it look" is
           swept rather than assumed */
        for(pi=1; pi<24 && fails<25; ++pi){
            for(pj=0; pj<64; ++pj) pb[pj]=0;
            pb[pi]='7';
            for(pk=0; pk<5; ++pk) chk(pb, BASES[pk]);
        }
        /* and a leading NUL in front of every one of the 256 byte values */
        for(pi=1; pi<256 && fails<25; ++pi){
            for(pj=0; pj<64; ++pj) pb[pj]=0;
            pb[0]=0; pb[1]=(char)pi; pb[2]='4'; pb[3]='2';
            for(pk=0; pk<5; ++pk) chk(pb, BASES[pk]);
        }
    }

    if(!fails) printf("CORRECTNESS: PASS (RtlCharToInteger vs live + oracle: STATUS+Value, edges + all 256 bytes x positions x bases + 3M fuzz\n+ PLANTED BYTES after a terminator, which literals cannot express)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
