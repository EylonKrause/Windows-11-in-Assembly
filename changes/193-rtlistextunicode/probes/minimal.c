/* Isolate the remaining RtlIsTextUnicode disagreement by EXHAUSTIVE enumeration of short buffers.
 * refcheck.c is down to 9189 mismatches of 3 000 000, every one of them only in the ILLEGAL_CHARS
 * bit, and every one of them on the byte alphabet {00,09,0A,0D,1A,20,30,61,FE,FF}. Rather than
 * guess at the CR/LF counter again, enumerate ALL buffers of length 4 and 6 over that alphabet
 * (10^4 and 10^6) and print the SHORTEST disagreements -- a minimal counterexample pins the rule.
 * Build: cl /nologo /O2 minimal.c && minimal.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef BOOLEAN (__stdcall *F)(const void*, int, int*);
static F live;
static const unsigned char A[10] = {0x00,0x09,0x0A,0x0D,0x1A,0x20,0x30,0x61,0xFE,0xFF};

/* the refcheck model, with its internals exposed so a counterexample can be explained */
typedef struct { unsigned crlf, zero, hi_var, lo_var, illegal_cnt, n; } dbg_t;

static unsigned ref_flags(const unsigned char* p, int len, dbg_t* d){
    unsigned n_all = (unsigned)len >> 1;
    unsigned n = n_all > 256u ? 256u : n_all;
    if(n == 0) return 5;
    if(len == 2){
        unsigned u = p[0] | ((unsigned)p[1]<<8);
        if(u != 0 && (u>>8) == 0) return 5;
    } else if(len > 2){
        if(n_all <= 256u && ((unsigned)len & 1u)==0){
            unsigned last = p[(n-1)*2] | ((unsigned)p[(n-1)*2+1]<<8);
            if((last & 0xFF00u)==0) n = n-1;
        }
    }
    unsigned ctl=0, rctl=0, ill=0, crlf=0, zero=0;
    unsigned hi_var=0, lo_var=0, prev_hi=0, prev_lo=0;
    for(unsigned i=0;i<n;i++){
        unsigned u = p[i*2] | ((unsigned)p[i*2+1]<<8);
        unsigned lo = u & 0xFF, hi = u >> 8;
        if(u > 0x0D00u){
            if(u <= 0x3000u){ if(u==0x3000u) ctl++; else if(u==0x2000u) rctl++; }
            else { if(u==0xFFFEu) ill++; else if(u==0xFFFFu) ill++; }
        } else if(u == 0x0D00u) rctl++;
        else if(u > 0x20u){
            if(u==0x0900u) rctl++; else if(u==0x0A00u) rctl++; else if(u==0x0A0Du) ill++;
        } else if(u == 0x20u) ctl++;
        else { if(u==0) ill++; else if(u==9) ctl++; else if(u==0x0Au) ctl++; else if(u==0x0Du) ctl++; }

        if(lo==0x0Du){ if(prev_hi==0x0Au) crlf++; }
        else if(lo==0x0Au){ if(prev_hi==0x0Du) crlf++; }
        zero += (lo==0) + (hi==0);
        hi_var += (hi>prev_hi)?(hi-prev_hi):(prev_hi-hi); prev_hi = hi;
        lo_var += (lo>prev_lo)?(lo-prev_lo):(prev_lo-lo); prev_lo = lo;
    }
    /* the post-loop CR/LF test, on the LAST unit's own bytes */
    if(prev_lo==0x0Du && prev_hi==0x0Au) crlf++;
    else if(prev_lo==0x0Au && prev_hi==0x0Du) crlf++;

    unsigned zc;
    if(prev_hi != 0){ zc = zero; if(prev_hi==0x1Au) crlf++; }
    else            { zc = zero - 1; }

    unsigned cap = (unsigned)len; if(cap>512u) cap=512u;
    unsigned f = 0;
    if(lo_var < 0x7Fu){ if(hi_var==0) f = 1; else if(lo_var==0) f = 0x10; }
    if(3u*hi_var < lo_var) f |= 2;
    if(3u*lo_var < hi_var) f |= 0x20;
    if(ctl)  f |= 4;
    if(rctl) f |= 0x40;
    if(ill)  f |= 0x100;
    else if(crlf && crlf >= cap/40u) f |= 0x100;
    if((unsigned)len & 1u) f |= 0x200;
    if(zc) f |= 0x1000;
    { unsigned first = p[0] | ((unsigned)p[1]<<8);
      if(first==0xFEFFu) f |= 8; else if(first==0xFFFEu) f |= 0x80; }
    if(d){ d->crlf=crlf; d->zero=zero; d->hi_var=hi_var; d->lo_var=lo_var; d->illegal_cnt=ill; d->n=n; }
    return f;
}

static int sweep(int len){
    unsigned char b[16];
    int idx[16] = {0};
    long long total = 1; for(int i=0;i<len;i++) total *= 10;
    int shown = 0; long long bad = 0;
    for(long long t=0;t<total;t++){
        long long v = t;
        for(int i=0;i<len;i++){ b[i] = A[v % 10]; v /= 10; }
        int la = -1; live(b,len,&la);
        dbg_t d;
        unsigned rf = ref_flags(b,len,&d);
        if((unsigned)la != rf){
            ++bad;
            if(shown < 12){
                printf("  len=%d live=%04X ref=%04X diff=%04X  bytes:", len, la, rf, (unsigned)la ^ rf);
                for(int i=0;i<len;i++) printf(" %02X", b[i]);
                printf("   [n=%u crlf=%u zero=%u hi_var=%u lo_var=%u ill=%u]\n",
                       d.n, d.crlf, d.zero, d.hi_var, d.lo_var, d.illegal_cnt);
                ++shown;
            }
        }
    }
    printf("  length %d: %lld disagreements of %lld\n", len, bad, total);
    return (int)bad;
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    live = (F)GetProcAddress(h,"RtlIsTextUnicode");
    if(!live){ printf("missing export\n"); return 1; }
    printf("=== exhaustive over the alphabet {00,09,0A,0D,1A,20,30,61,FE,FF} ===\n");
    for(int len=2; len<=6; ++len) sweep(len);
    return 0;
}
