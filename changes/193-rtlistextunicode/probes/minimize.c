/* Delta-debug a failing RtlIsTextUnicode buffer down to a minimal counterexample.
 * refcheck.c now disagrees on 8578 of 3 000 000 cases, always only in ILLEGAL_CHARS, and only on
 * buffers of 20+ bytes -- exhaustive enumeration up to length 6 is clean. So instead of guessing
 * at the CR/LF counter again, take a known failing buffer and repeatedly delete 2-byte chunks for
 * as long as it still disagrees. Whatever survives is the rule.
 * Build: cl /nologo /O2 minimize.c && minimize.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOLEAN (__stdcall *F)(const void*, int, int*);
static F live;

static unsigned ref_flags(const unsigned char* p, int len,
                          unsigned* out_crlf, unsigned* out_n){
    unsigned n_all = (unsigned)len >> 1;
    unsigned n = n_all > 256u ? 256u : n_all;
    if(n == 0){ if(out_crlf)*out_crlf=0; if(out_n)*out_n=0; return 5; }
    if(len == 2){
        unsigned u = p[0] | ((unsigned)p[1]<<8);
        if(u != 0 && (u>>8) == 0){ if(out_crlf)*out_crlf=0; if(out_n)*out_n=0; return 5; }
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
    if(prev_lo==0x0Du){ if(prev_hi==0x0Au) crlf++; }
    else if(prev_lo==0x0Au){ if(prev_hi==0x0Du) crlf++; }
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
    if(out_crlf)*out_crlf=crlf; if(out_n)*out_n=n;
    return f;
}

static int differs(const unsigned char* b, int len){
    int la = -1; live(b,len,&la);
    return (unsigned)la != ref_flags(b,len,NULL,NULL);
}

static void show(const char* tag, const unsigned char* b, int len){
    int la=-1; live(b,len,&la);
    unsigned crlf=0,n=0; unsigned rf = ref_flags(b,len,&crlf,&n);
    printf("%s len=%d live=%04X ref=%04X  n=%u crlf=%u  bytes:", tag, len, la, rf, n, crlf);
    for(int i=0;i<len;i++) printf(" %02X", b[i]);
    printf("\n");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    live = (F)GetProcAddress(h,"RtlIsTextUnicode");
    if(!live){ printf("missing export\n"); return 1; }

    static const unsigned char SEED[] = {
        0x1A,0xFE,0x1A,0x30,0x20,0x61,0x09,0x09,0x20,0x00,0x20,
        0x30,0x61,0x0D,0x30,0x0A,0xFF,0x20,0x0A,0x0A,0x20,0x1A };
    unsigned char b[64]; int len = (int)sizeof(SEED);
    memcpy(b, SEED, len);
    show("seed     :", b, len);
    if(!differs(b,len)){ printf("seed does not differ -- nothing to minimize\n"); return 1; }

    /* delete 2-byte chunks while the disagreement survives */
    int changed = 1;
    while(changed){
        changed = 0;
        for(int pos=0; pos+2<=len; pos+=2){
            unsigned char t[64]; int tl = len-2;
            memcpy(t, b, pos);
            memcpy(t+pos, b+pos+2, len-pos-2);
            if(tl>=2 && differs(t,tl)){ memcpy(b,t,tl); len=tl; changed=1; break; }
        }
    }
    show("minimal  :", b, len);

    /* now try simplifying each remaining byte towards 0x61 ('a', an inert value) */
    changed = 1;
    while(changed){
        changed = 0;
        for(int i=0;i<len;i++){
            if(b[i]==0x61) continue;
            unsigned char save = b[i];
            b[i] = 0x61;
            if(differs(b,len)) { changed = 1; }
            else b[i] = save;
        }
    }
    show("simplified:", b, len);

    /* report every single-byte neighbour that still differs, to expose the rule */
    printf("\nsingle-byte perturbations that KEEP the disagreement:\n");
    for(int i=0;i<len;i++){
        unsigned char save = b[i];
        int lo=-1, hi=-1;
        for(int v=0; v<256; ++v){
            b[i]=(unsigned char)v;
            if(differs(b,len)){ if(lo<0) lo=v; hi=v; }
        }
        b[i]=save;
        printf("  byte %2d (=%02X): values %02X..%02X still differ\n", i, save, lo, hi);
    }
    return 0;
}
