// changes/293-systemtimetofiletime/correctness.c
//
// Gate 1 for change 293. Compares wia_systemtime_to_filetime against both reference.c (the oracle)
// and the LIVE kernel32!SystemTimeToFileTime resolved with GetProcAddress on this PC. A single
// mismatch fails.
//
// What is compared on every case, not just the return value:
//   * the BOOL return;
//   * all 8 output bytes;
//   * that on failure the output qword is left exactly as the caller had it (sentinel);
//   * 24 guard bytes on each side of the FILETIME, to prove nothing is written past the logical
//     end in either direction;
//   * TEB->LastErrorValue and TEB->LastStatusValue after the call, so "the last error is not
//     touched on success" and "87 / STATUS_INVALID_PARAMETER on failure" are gated, not assumed.
//     Both TEB offsets are re-derived at startup through GetLastError / RtlGetLastNtStatus and the
//     test refuses to run if either does not agree.
//
// CORPUS
//   1. every day of the whole legal domain, 1601-01-01 .. 30827-12-31, all 10,674,942 of them,
//      each also checked against a running day counter that owes nothing to any closed form;
//   2. the full Month x Day grid including invalid values, for every year from 1600 to 30828;
//   3. every field's range edges, both ends, plus 0x7FFF / 0x8000 / 0xFFFF CSHORT wraparound;
//   4. all 65536 values of wDayOfWeek on one valid date, the field the live export ignores;
//   5. the structure at every byte alignment 0..63;
//   6. the structure ending exactly at a page boundary with the next page PAGE_NOACCESS, and
//      starting exactly at a page boundary with the PREVIOUS page PAGE_NOACCESS;
//   7. round-trip: live FileTimeToSystemTime over the domain, converted back, must reproduce the
//      instant exactly;
//   8. two randomized fuzz sets with a FIXED seed, one plausible, one with all eight fields drawn
//      from the full unconstrained 16-bit range.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <intrin.h>

typedef struct { unsigned short wYear,wMonth,wDayOfWeek,wDay,wHour,wMinute,wSecond,wMilliseconds; } WIA_ST;

extern int wia_systemtime_to_filetime(const WIA_ST*, unsigned long long*);   /* impl.asm */
int ref_systemtime_to_filetime(const WIA_ST*, unsigned long long*);          /* reference.c */
int wia_ref_is_leap(int);
int wia_ref_days_in_month(int,int);

typedef BOOL (WINAPI *fnST2FT)(const SYSTEMTIME*, LPFILETIME);
typedef BOOL (WINAPI *fnFT2ST)(const FILETIME*, LPSYSTEMTIME);
typedef LONG (WINAPI *fnLastNt)(void);
static fnST2FT sys;
static fnFT2ST sysback;

static BYTE* g_teb;
#define LASTERR  (*(DWORD*)(g_teb+0x68))
#define LASTSTAT (*(LONG *)(g_teb+0x1250))

#define SENT  0x5A5A5A5A5A5A5A5AULL
#define GUARD 0xA7
#define PRE_ERR   0x11111111u
#define PRE_STAT  0x22222222

static long long cases = 0;
static int fails = 0;

/* A 64-byte block: 24 guard bytes, the 8-byte output, 32 guard bytes. */
typedef struct { unsigned char pre[24]; unsigned long long t; unsigned char post[32]; } BLOCK;
static unsigned char g_guard[32];
static void block_init(BLOCK* b){ memset(b,GUARD,sizeof(*b)); b->t = SENT; }
static int block_guards_ok(const BLOCK* b){
    return memcmp(b->pre,g_guard,24)==0 && memcmp(b->post,g_guard,32)==0;
}

static void report(const WIA_ST* s, const char* what,
                   int rl, unsigned long long tl, DWORD el, LONG sl,
                   int ro, unsigned long long to, DWORD eo, LONG so,
                   int rr, unsigned long long tr)
{
    if(fails < 25)
        printf("FAIL[%s] %u-%02u-%02u %02u:%02u:%02u.%03u dow=%u\n"
               "      live{r=%d t=%llu err=0x%08lX st=0x%08lX}\n"
               "      ours{r=%d t=%llu err=0x%08lX st=0x%08lX}\n"
               "      ref {r=%d t=%llu}\n",
               what, s->wYear,s->wMonth,s->wDay,s->wHour,s->wMinute,s->wSecond,s->wMilliseconds,
               s->wDayOfWeek, rl,tl,(unsigned long)el,(unsigned long)sl,
               ro,to,(unsigned long)eo,(unsigned long)so, rr,tr);
    ++fails;
}

/* The full three-way comparison. `p` may point anywhere (alignment / page tests). */
static void chk_at(const WIA_ST* p)
{
    BLOCK bl, bo;
    block_init(&bl); block_init(&bo);

    LASTERR = PRE_ERR; LASTSTAT = PRE_STAT;
    int rl = sys((const SYSTEMTIME*)p, (LPFILETIME)&bl.t) ? 1 : 0;
    DWORD el = LASTERR; LONG sl = LASTSTAT;

    LASTERR = PRE_ERR; LASTSTAT = PRE_STAT;
    int ro = wia_systemtime_to_filetime(p, &bo.t) ? 1 : 0;
    DWORD eo = LASTERR; LONG so = LASTSTAT;

    unsigned long long tr = SENT;
    int rr = ref_systemtime_to_filetime(p, &tr) ? 1 : 0;

    ++cases;
    const char* what = 0;
    if      (rl != ro || rl != rr)                         what = "return";
    else if (rl && (bl.t != bo.t || bl.t != tr))           what = "value";
    else if (!rl && (bl.t != SENT || bo.t != SENT || tr != SENT)) what = "wrote-on-failure";
    else if (el != eo || sl != so)                         what = "lasterror";
    else if (!block_guards_ok(&bl) || !block_guards_ok(&bo)) what = "guard-bytes";
    if (what) report(p, what, rl,bl.t,el,sl, ro,bo.t,eo,so, rr,tr);
}

static void chk(const WIA_ST* s){ chk_at(s); }

static WIA_ST mk(int y,int mo,int dow,int d,int h,int mi,int se,int ms){
    WIA_ST s; s.wYear=(unsigned short)y; s.wMonth=(unsigned short)mo; s.wDayOfWeek=(unsigned short)dow;
    s.wDay=(unsigned short)d; s.wHour=(unsigned short)h; s.wMinute=(unsigned short)mi;
    s.wSecond=(unsigned short)se; s.wMilliseconds=(unsigned short)ms; return s;
}

int main(void)
{
    HMODULE k32 = LoadLibraryW(L"kernel32.dll");
    HMODULE ntd = LoadLibraryW(L"ntdll.dll");
    sys     = (fnST2FT)GetProcAddress(k32,"SystemTimeToFileTime");
    sysback = (fnFT2ST)GetProcAddress(k32,"FileTimeToSystemTime");
    fnLastNt lastnt = (fnLastNt)GetProcAddress(ntd,"RtlGetLastNtStatus");
    if(!sys || !sysback || !lastnt){ printf("missing export\n"); return 2; }

    memset(g_guard, GUARD, sizeof(g_guard));

    /* Derive and VERIFY the two TEB fields before relying on them. */
    g_teb = (BYTE*)__readgsqword(0x30);
    SetLastError(0x13572468);
    if(LASTERR != 0x13572468){ printf("TEB LastErrorValue offset wrong\n"); return 2; }
    LASTERR = 0x2468ACE0;
    if(GetLastError() != 0x2468ACE0){ printf("TEB LastErrorValue offset wrong (read)\n"); return 2; }
    LASTSTAT = (LONG)0xC0000999;
    if(lastnt() != (LONG)0xC0000999){ printf("TEB LastStatusValue offset wrong\n"); return 2; }

    /* ---- 1. every day of the whole legal domain, against a running day counter ---- */
    {
        long long counter = 0; int cfails = 0;
        for(int y=1601; y<=30827 && fails<25; ++y)
            for(int m=1;m<=12;++m){
                int dim = wia_ref_days_in_month(y,m);
                for(int d=1; d<=dim; ++d){
                    WIA_ST s = mk(y,m,(d+m)%7,d, d%24, (m*5)%60, y%60, y%1000);
                    unsigned long long want = (unsigned long long)(
                        counter*864000000000LL + (long long)s.wHour*36000000000LL
                        + (long long)s.wMinute*600000000LL + (long long)s.wSecond*10000000LL
                        + (long long)s.wMilliseconds*10000LL);
                    unsigned long long got = 0;
                    if(!wia_systemtime_to_filetime(&s,&got) || got != want){
                        if(cfails<10) printf("DAYCOUNT FAIL %d-%02d-%02d ours=%llu want=%llu\n",y,m,d,got,want);
                        ++cfails; ++fails;
                    }
                    chk(&s);
                    ++counter;
                }
            }
        printf("[1] whole domain day by day: %lld days, %d day-counter mismatches\n", counter, cfails);
    }

    /* ---- 2. full Month x Day grid, including invalid values, every year 1600..30828 ---- */
    for(int y=1600; y<=30828 && fails<25; ++y)
        for(int m=0;m<=13;++m)
            for(int d=0;d<=32;++d){ WIA_ST s = mk(y,m,3,d,13,45,7,123); chk(&s); }
    printf("[2] month/day grid 1600..30828: cases=%lld fails=%d\n", cases, fails);

    /* ---- 3. every field's range edges, both ends, plus CSHORT wraparound ---- */
    {
        static const long lim[7] = {30827,12,31,23,59,59,999};
        static const unsigned short odd[] = {0,1,2,0x7FFE,0x7FFF,0x8000,0x8001,0xFFFE,0xFFFF,
                                             1599,1600,1601,1602,30826,30827,30828,30829};
        for(int fi=0; fi<7 && fails<25; ++fi){
            for(long v=-6; v<=lim[fi]+6; ++v){
                WIA_ST s = mk(2023,6,3,15,13,45,7,123);
                unsigned short vv=(unsigned short)(short)v;
                switch(fi){case 0:s.wYear=vv;break;case 1:s.wMonth=vv;break;case 2:s.wDay=vv;break;
                           case 3:s.wHour=vv;break;case 4:s.wMinute=vv;break;case 5:s.wSecond=vv;break;
                           default:s.wMilliseconds=vv;}
                chk(&s);
            }
            for(unsigned i=0;i<sizeof(odd)/sizeof(odd[0]);++i){
                WIA_ST s = mk(2023,6,3,15,13,45,7,123);
                switch(fi){case 0:s.wYear=odd[i];break;case 1:s.wMonth=odd[i];break;case 2:s.wDay=odd[i];break;
                           case 3:s.wHour=odd[i];break;case 4:s.wMinute=odd[i];break;case 5:s.wSecond=odd[i];break;
                           default:s.wMilliseconds=odd[i];}
                chk(&s);
            }
        }
        /* February at every century and quadricentennial boundary in the domain */
        for(int y=1601; y<=30827 && fails<25; ++y)
            if(y%4==0 || y%100==0 || y%400==0 || y%100==99){
                WIA_ST a = mk(y,2,0,28,0,0,0,0), b = mk(y,2,0,29,0,0,0,0), c = mk(y,2,0,30,0,0,0,0);
                chk(&a); chk(&b); chk(&c);
            }
        printf("[3] field edges + every February boundary: cases=%lld fails=%d\n", cases, fails);
    }

    /* ---- 4. all 65536 wDayOfWeek values: the field the live export never reads ---- */
    {
        WIA_ST b0 = mk(2023,6,0,15,13,45,7,123);
        unsigned long long base=0; wia_systemtime_to_filetime(&b0, &base);
        for(long dw=0; dw<=65535 && fails<25; ++dw){
            WIA_ST s = mk(2023,6,dw,15,13,45,7,123);
            unsigned long long t=0;
            if(!wia_systemtime_to_filetime(&s,&t) || t!=base){
                printf("DOW FAIL dow=%ld\n",dw); ++fails; break;
            }
            chk(&s);
        }
        printf("[4] wDayOfWeek 0..65535 ignored: cases=%lld fails=%d\n", cases, fails);
    }

    /* ---- 5/6. alignment sweep and page-boundary placement ---- */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        DWORD pg = si.dwPageSize, old = 0;
        /* three committed pages; the outer two then flipped to PAGE_NOACCESS so that a single byte
           read before or after the 16-byte structure raises an access violation. */
        BYTE* base = (BYTE*)VirtualAlloc(NULL, pg*3, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
        if(!base){ printf("VirtualAlloc failed\n"); return 2; }
        BYTE* mid = base + pg;
        if(!VirtualProtect(base,      pg, PAGE_NOACCESS, &old) ||
           !VirtualProtect(base+2*pg, pg, PAGE_NOACCESS, &old)){
            printf("VirtualProtect failed\n"); return 2; }

        WIA_ST good = mk(2023,6,3,15,13,45,7,123);
        WIA_ST bad  = mk(2023,13,3,15,13,45,7,123);

        for(int off=0; off<64 && fails<25; ++off){
            memcpy(mid+off,&good,16); chk_at((const WIA_ST*)(mid+off));
            memcpy(mid+off,&bad ,16); chk_at((const WIA_ST*)(mid+off));
        }
        /* last byte of the structure == last byte of the committed page; next page NOACCESS */
        memcpy(mid+pg-16,&good,16); chk_at((const WIA_ST*)(mid+pg-16));
        memcpy(mid+pg-16,&bad ,16); chk_at((const WIA_ST*)(mid+pg-16));
        /* first byte of the structure == first byte of the committed page; previous page NOACCESS */
        memcpy(mid,&good,16); chk_at((const WIA_ST*)mid);
        memcpy(mid,&bad ,16); chk_at((const WIA_ST*)mid);
        printf("[5/6] alignment 0..63 + both page edges against PAGE_NOACCESS: cases=%lld fails=%d\n",
               cases, fails);
        VirtualFree(base,0,MEM_RELEASE);
    }

    /* ---- 7. round-trip through the live FileTimeToSystemTime ---- */
    {
        int rt = 0, rtbad = 0;
        for(long long d=0; d<10674942LL && fails<25; d += 53){
            long long tt = d*864000000000LL + 45296789000LL;   /* 12:34:56.7890 into the day */
            FILETIME f; *(unsigned long long*)&f = (unsigned long long)tt;
            SYSTEMTIME st;
            if(!sysback(&f,&st)) continue;
            WIA_ST s; memcpy(&s,&st,16);
            unsigned long long back=0;
            int r = wia_systemtime_to_filetime(&s,&back);
            /* FileTimeToSystemTime truncates to whole milliseconds; compare on that grid */
            unsigned long long want = (unsigned long long)((tt/10000)*10000);
            if(!r || back != want){ if(rtbad<10) printf("ROUNDTRIP FAIL day=%lld back=%llu want=%llu\n",d,back,want); ++rtbad; ++fails; }
            chk(&s);
            ++rt;
        }
        printf("[7] round-trips through live FileTimeToSystemTime: %d, %d bad\n", rt, rtbad);
    }

    /* ---- 8. fuzz, fixed seed ---- */
    {
        unsigned long long sd = 0x243f6a8885a308d3ULL;
        for(int i=0;i<3000000 && fails<25;++i){
            sd = sd*6364136223846793005ULL + 1442695040888963407ULL;
            WIA_ST s;
            s.wYear=(unsigned short)((sd>>3)%40000); s.wMonth=(unsigned short)((sd>>19)%16);
            s.wDayOfWeek=(unsigned short)(sd>>13);   s.wDay=(unsigned short)((sd>>23)%34);
            s.wHour=(unsigned short)((sd>>29)%26);   s.wMinute=(unsigned short)((sd>>35)%63);
            s.wSecond=(unsigned short)((sd>>41)%63); s.wMilliseconds=(unsigned short)((sd>>47)%1024);
            chk(&s);
        }
        for(int i=0;i<3000000 && fails<25;++i){
            sd = sd*6364136223846793005ULL + 1442695040888963407ULL;
            WIA_ST s;
            s.wYear=(unsigned short)(sd>>1);   s.wMonth=(unsigned short)(sd>>17);
            s.wDayOfWeek=(unsigned short)(sd>>33); s.wDay=(unsigned short)(sd>>49);
            s.wHour=(unsigned short)(sd>>5);   s.wMinute=(unsigned short)(sd>>21);
            s.wSecond=(unsigned short)(sd>>37); s.wMilliseconds=(unsigned short)(sd>>53);
            chk(&s);
        }
        printf("[8] fuzz (fixed seed, 3M plausible + 3M unconstrained): cases=%lld fails=%d\n", cases, fails);
    }

    SetLastError(0);
    if(!fails)
        printf("CORRECTNESS: PASS (kernel32!SystemTimeToFileTime vs live + oracle: %lld cases, "
               "whole 10,674,942-day domain, every field edge, all 65536 wDayOfWeek values, "
               "64 alignments, both page edges vs PAGE_NOACCESS, 201k round-trips, 6M fuzz; "
               "return + 8 output bytes + guard bytes + LastError + LastStatus all compared)\n", cases);
    else
        printf("CORRECTNESS: FAIL (%d mismatches over %lld cases)\n", fails, cases);
    return fails ? 1 : 0;
}
