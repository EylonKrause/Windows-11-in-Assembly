/* RtlIsTextUnicode, candidate reference, fuzzed against the live export.
 *
 * Black-box probing (itu.c, itu2.c, itu3.c) settled the cap, CONTROLS, ODD_LENGTH and NULL_BYTES
 * but could not explain ASCII16 or STATISTICS. The shipped code answered both:
 *   dumpbin /disasm ntdll.dll, RVA 0x000D3A10.
 *
 * The model read out of it:
 *   n = min(len/2, 256)                       <- "mov r14d,100h ; cmova edx,r14d", and it is why
 *                                                the measured cost saturates at ~735 ns
 *   n == 0                    -> *lpi = 5, FALSE
 *   len == 2 and the single unit is nonzero with a zero high byte -> *lpi = 5, FALSE
 *   len > 2, len even, len/2 <= 256, and the LAST unit has a zero high byte -> drop that unit
 *
 *   per unit: count U+0020 U+0009 U+000A U+000D U+3000            -> CONTROLS
 *             count U+0900 U+0A00 U+0D00 U+2000                   -> REVERSE_CONTROLS
 *             count U+0000 U+0A0D U+FFFE U+FFFF                   -> ILLEGAL_CHARS
 *             count zero BYTES
 *             crlf++ when (lo==0x0D and prev_hi==0x0A) or (lo==0x0A and prev_hi==0x0D)
 *             hi_var += |hi - prev_hi| ; lo_var += |lo - prev_lo|   (prev starts at 0)
 *   after the loop: if last_HI != 0 { zc_adj = zero_count; if (last_hi == 0x1A) crlf++; }
 *                   else             zc_adj = zero_count - 1;
 *                   (the last HIGH byte, the loop exit overwrites the prev_lo slot with it)
 *
 *   ASCII16          iff lo_var < 0x7F and hi_var == 0
 *   REVERSE_ASCII16  iff lo_var < 0x7F and hi_var != 0 and lo_var == 0
 *   STATISTICS       iff 3*hi_var <  lo_var        <- the "total variation" test; this is what the
 *   REVERSE_STATS    iff 3*lo_var <  hi_var           64-identical-unit boundary low>3*high was
 *   ILLEGAL_CHARS    iff any illegal counter != 0, OR crlf >= min(len,512)/40
 *   ODD_LENGTH       iff len is odd
 *   NULL_BYTES       iff zc_adj != 0
 *   Signature        iff first unit == u+feff ; REVERSE_SIGNATURE iff it is u+fffe
 *   if (lpi) *lpi &= flags, and the BOOL is computed from the MASKED value
 *   BOOL: (f & 0xB08)==8 -> TRUE ; f & 0xF0 -> FALSE ; f & 0xF00 -> FALSE ; f & 0xF00F -> TRUE ;
 *         else FALSE
 *
 * Scope: the shipped code also has a dbcs lead-byte pass that can lower the statistics multiplier
 * from 3 to 2 or 1 and set IS_TEXT_UNICODE_DBCS_LEADBYTE. It is gated on an ntdll-internal
 * code-page table AND on the caller explicitly asking for bit 0x400, and on a single-byte ANSI
 * code page it never runs. This reference implements the multiplier as a constant 3; the fuzz
 * below therefore also reports how often bit 0x400 appears, which must be zero for the claim to
 * hold on this machine.
 *
 * Build: cl /nologo /O2 refcheck.c && refcheck.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOLEAN (__stdcall *F)(const void*, int, int*);
static F live;

static int ref_itu(const void* buf, int len, int* lpi){
    const unsigned char* p = (const unsigned char*)buf;
    unsigned n_all = (unsigned)len >> 1;
    unsigned n = n_all > 256u ? 256u : n_all;

    if(n == 0){ if(lpi) *lpi = 5; return 0; }

    if(len == 2){
        unsigned u = (unsigned)p[0] | ((unsigned)p[1] << 8);
        if(u != 0 && (u >> 8) == 0){ if(lpi) *lpi = 5; return 0; }
    } else if(len > 2){
        if(n_all <= 256u && ((unsigned)len & 1u) == 0){
            unsigned last = (unsigned)p[(n-1)*2] | ((unsigned)p[(n-1)*2+1] << 8);
            if((last & 0xFF00u) == 0) n = n - 1;
        }
    }

    unsigned c0020=0,c0009=0,c000A=0,c000D=0,c3000=0;
    unsigned c0900=0,c0A00=0,c0D00=0,c2000=0;
    unsigned c0000=0,c0A0D=0,cFFFE=0,cFFFF=0;
    unsigned crlf=0, zero=0;
    unsigned hi_var=0, lo_var=0, prev_hi=0, prev_lo=0;

    for(unsigned i=0;i<n;i++){
        unsigned u  = (unsigned)p[i*2] | ((unsigned)p[i*2+1] << 8);
        unsigned lo = u & 0xFFu, hi = u >> 8;

        if(u > 0x0D00u){
            if(u <= 0x3000u){
                if(u == 0x3000u) c3000++;
                else if(u == 0x2000u) c2000++;
            } else {
                if(u == 0xFFFEu) cFFFE++;
                else if(u == 0xFFFFu) cFFFF++;
            }
        } else if(u == 0x0D00u){
            c0D00++;
        } else if(u > 0x20u){
            if(u == 0x0900u) c0900++;
            else if(u == 0x0A00u) c0A00++;
            else if(u == 0x0A0Du) c0A0D++;
        } else if(u == 0x20u){
            c0020++;
        } else {
            if(u == 0) c0000++;
            else if(u == 9) c0009++;
            else if(u == 0x0Au) c000A++;
            else if(u == 0x0Du) c000D++;
        }

        if(lo == 0x0Du){ if(prev_hi == 0x0Au) crlf++; }
        else if(lo == 0x0Au){ if(prev_hi == 0x0Du) crlf++; }

        zero += (lo == 0) + (hi == 0);

        hi_var += (hi > prev_hi) ? (hi - prev_hi) : (prev_hi - hi);
        prev_hi = hi;
        lo_var += (lo > prev_lo) ? (lo - prev_lo) : (prev_lo - lo);
        prev_lo = lo;
    }

    /* One more cr/lf test after the loop, this time against the last unit's own high byte --
       the loop-exit block has already written [rsp+14h] for the final unit, so the same two
       compares at 0x3CEB/0x3CF4 now test whether the last unit is 0x0A0D or 0x0D0A. */
    if(n){
        if(prev_lo == 0x0Du){ if(prev_hi == 0x0Au) crlf++; }
        else if(prev_lo == 0x0Au){ if(prev_hi == 0x0Du) crlf++; }
    }

    /* The loop-exit code overwrites [rsp+30h] (prev_lo) with [rsp+14h] (prev_hi) before this
       test, so the post-loop condition is on the LAST HIGH BYTE, not the last low byte.
       Reading it as the low byte produced 84090 mismatches of 3000000 -- every one of them
       only in the NULL_BYTES bit, which is what pointed at this slot. */
    unsigned zc_adj;
    if(prev_hi != 0){ zc_adj = zero; if(prev_hi == 0x1Au) crlf++; }
    else            { zc_adj = zero - 1; }

    unsigned cap = (unsigned)len; if(cap > 512u) cap = 512u;

    unsigned f = 0;
    if(lo_var < 0x7Fu){
        if(hi_var == 0)      f = 0x0001u;          /* ASCII16 */
        else if(lo_var == 0) f = 0x0010u;          /* REVERSE_ASCII16 */
    }
    if(3u*hi_var <  lo_var) f |= 0x0002u;          /* STATISTICS */
    if(3u*lo_var <  hi_var) f |= 0x0020u;          /* REVERSE_STATISTICS */
    if(c0020+c0009+c000A+c000D+c3000) f |= 0x0004u;
    if(c0900+c0A00+c0D00+c2000)       f |= 0x0040u;
    if(c0000+c0A0D+cFFFE+cFFFF)       f |= 0x0100u;
    else if(crlf && crlf >= cap/40u)  f |= 0x0100u;
    if((unsigned)len & 1u)            f |= 0x0200u;
    if(zc_adj != 0)                   f |= 0x1000u;
    {
        unsigned first = (unsigned)p[0] | ((unsigned)p[1] << 8);
        if(first == 0xFEFFu)      f |= 0x0008u;
        else if(first == 0xFFFEu) f |= 0x0080u;
    }

    if(lpi){ *lpi &= (int)f; f = (unsigned)*lpi; }

    if((f & 0xB08u) == 8u) return 1;
    if(f & 0x00F0u) return 0;
    if(f & 0x0F00u) return 0;
    if(f & 0xF00Fu) return 1;
    return 0;
}

static unsigned long sd = 777;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    live = (F)GetProcAddress(h,"RtlIsTextUnicode");
    if(!live){ printf("missing export\n"); return 1; }

    static unsigned char buf[2600];
    long long bad=0, N=3000000, dbcs=0;
    int shown=0;

    /* ask-masks: NULL, all, and random subsets, the masking interacts with the BOOL */
    for(long long t=0;t<N;t++){
        int len;
        unsigned shape = rnd()%10;
        if(shape<3)      len = (int)(rnd()%12);              /* tiny: the len<=2 special cases */
        else if(shape<6) len = (int)(rnd()%80);
        else if(shape<8) len = (int)(500 + rnd()%40);        /* around the 512-byte cap */
        else             len = (int)(rnd()%2500);
        if(len > 2560) len = 2560;

        unsigned kind = rnd()%8;
        for(int i=0;i<len;i++){
            switch(kind){
                case 0: buf[i] = (unsigned char)(rnd()&0xFF); break;                 /* pure random */
                case 1: buf[i] = (i&1) ? 0 : (unsigned char)(0x20+rnd()%0x5F); break;/* LE ASCII text */
                case 2: buf[i] = (i&1) ? (unsigned char)(0x20+rnd()%0x5F) : 0; break;/* BE ASCII text */
                case 3: buf[i] = (unsigned char)(0x20+rnd()%0x5F); break;            /* ANSI text */
                case 4: buf[i] = (i&1) ? (unsigned char)(rnd()%4) : (unsigned char)(rnd()&0xFF); break;
                case 5: buf[i] = (unsigned char)((rnd()%3)?0x61:0x00); break;
                case 6: { static const unsigned char S[] = {0,9,0x0A,0x0D,0x20,0x1A,0xFE,0xFF,0x30,0x61};
                          buf[i] = S[rnd()%10]; } break;
                default: buf[i] = (i&1) ? (unsigned char)(rnd()%2 ? 0 : 0xFF)
                                        : (unsigned char)(rnd()&0xFF); break;
            }
        }
        /* sometimes plant a BOM */
        if(len>=2 && rnd()%6==0){ if(rnd()&1){ buf[0]=0xFF; buf[1]=0xFE; } else { buf[0]=0xFE; buf[1]=0xFF; } }

        int use_null = (rnd()%4)==0;
        int mask = use_null ? 0 : (int)(rnd()%3 ? -1 : (int)(rnd() & 0x3FFF));

        int la = mask, lb = mask;
        int ra = live(buf,len, use_null?NULL:&la) ? 1 : 0;
        int rb = ref_itu(buf,len, use_null?NULL:&lb);
        if(!use_null && (la & 0x400)) ++dbcs;
        if(ra!=rb || (!use_null && la!=lb)){
            if(shown<10){
                printf("  MISMATCH len=%d kind=%u mask=%08X null=%d : live=%d/%08X ref=%d/%08X\n",
                       len, kind, mask, use_null, ra, la, rb, lb);
                printf("    bytes:");
                for(int i=0;i<len && i<24;i++) printf(" %02X", buf[i]);
                printf("\n");
                ++shown;
            }
            ++bad;
        }
    }
    printf("  %lld mismatches of %lld  %s\n", bad, N, bad?"RULE IS WRONG":"RULE CONFIRMED");
    printf("  DBCS_LEADBYTE seen %lld times (must be 0 for the constant-3 multiplier to hold)\n", dbcs);
    return bad?1:0;
}
