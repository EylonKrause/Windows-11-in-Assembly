// changes/293-systemtimetofiletime/probes/refcheck.c
// Probe 4. Procedure step 4: get reference.c passing against the LIVE export BEFORE writing any
// assembly. A reference that disagrees with the live export means the contract is wrong, and
// finding that out after writing the assembly wastes the assembly.
//
// Three-way: live kernel32!SystemTimeToFileTime  vs  reference.c  vs  a running day counter that
// is simply incremented one day at a time across the entire legal domain (1601-01-01 .. 30827-12-31,
// all 10,674,942 days), which owes nothing to any closed form.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef struct { unsigned short wYear,wMonth,wDayOfWeek,wDay,wHour,wMinute,wSecond,wMilliseconds; } WIA_ST;
int ref_systemtime_to_filetime(const WIA_ST*, unsigned long long*);
int wia_ref_is_leap(int);
int wia_ref_days_in_month(int,int);

typedef BOOL (WINAPI *fnST2FT)(const SYSTEMTIME*, LPFILETIME);
static fnST2FT sys;
#define SENT 0x5A5A5A5A5A5A5A5AULL
static int fails=0; static long long n=0;

static void chk(const WIA_ST* s){
    FILETIME f; *(ULONGLONG*)&f = SENT;
    BOOL a = sys((const SYSTEMTIME*)s, &f);
    ULONGLONG at = ((ULONGLONG)f.dwHighDateTime<<32)|f.dwLowDateTime;
    unsigned long long bt = SENT; int b = ref_systemtime_to_filetime(s,&bt);
    int ok = ((!!a)==(!!b));
    if(a){ if(at!=bt) ok=0; } else { if(at!=SENT || bt!=SENT) ok=0; }
    ++n;
    if(!ok){ if(fails<20) printf("FAIL %u-%02u-%02u %02u:%02u:%02u.%03u dow=%u live{%d,%llu} ref{%d,%llu}\n",
        s->wYear,s->wMonth,s->wDay,s->wHour,s->wMinute,s->wSecond,s->wMilliseconds,s->wDayOfWeek,a,at,b,bt); ++fails; }
}

int main(void){
    sys=(fnST2FT)GetProcAddress(LoadLibraryW(L"kernel32.dll"),"SystemTimeToFileTime");
    if(!sys){ printf("no export\n"); return 2; }

    // ---- the whole legal domain, day by day, against an incrementing counter ----
    long long counter = 0; int cfails=0;
    for(int y=1601; y<=30827 && fails<20; ++y){
        int leap = wia_ref_is_leap(y);
        for(int m=1;m<=12;++m){
            int dim = wia_ref_days_in_month(y,m);
            for(int d=1; d<=dim; ++d){
                WIA_ST s={(unsigned short)y,(unsigned short)m,(unsigned short)((d+m)%7),
                          (unsigned short)d,(unsigned short)(d%24),(unsigned short)(m*5%60),
                          (unsigned short)(y%60),(unsigned short)(y%1000)};
                FILETIME f; *(ULONGLONG*)&f=SENT;
                BOOL a = sys((const SYSTEMTIME*)&s,&f);
                ULONGLONG at=((ULONGLONG)f.dwHighDateTime<<32)|f.dwLowDateTime;
                ULONGLONG want = (ULONGLONG)(counter*864000000000LL + (long long)s.wHour*36000000000LL
                                 + (long long)s.wMinute*600000000LL + (long long)s.wSecond*10000000LL
                                 + (long long)s.wMilliseconds*10000LL);
                unsigned long long bt=SENT; int b=ref_systemtime_to_filetime(&s,&bt);
                if(!a||!b||at!=want||bt!=want){ if(cfails<10) printf("DAYCOUNT FAIL %d-%02d-%02d live{%d,%llu} ref{%d,%llu} want=%llu\n",y,m,d,a,at,b,bt,want); ++cfails; ++fails; }
                ++counter; ++n;
                (void)leap;
            }
        }
    }
    printf("day-by-day over the whole domain: %lld days, %d failures\n", counter, cfails);

    // ---- every month/day combination including the invalid ones, every year ----
    for(int y=1600; y<=30828 && fails<20; ++y)
        for(int m=0;m<=13;++m)
            for(int d=0;d<=32;++d){
                WIA_ST s={(unsigned short)y,(unsigned short)m,3,(unsigned short)d,13,45,7,123};
                chk(&s);
            }
    printf("after month/day grid over 1600..30828: n=%lld failures=%d\n", n, fails);

    // ---- every field edge, including CSHORT wraparound ----
    {
        static const int lim[7] = {30827,12,31,23,59,59,999};
        for(int fi=0; fi<7 && fails<20; ++fi){
            for(long v=-4; v<=lim[fi]+4; ++v){
                WIA_ST s={2023,6,3,15,13,45,7,123};
                unsigned short vv=(unsigned short)(short)v;
                switch(fi){case 0:s.wYear=vv;break;case 1:s.wMonth=vv;break;case 2:s.wDay=vv;break;
                           case 3:s.wHour=vv;break;case 4:s.wMinute=vv;break;case 5:s.wSecond=vv;break;
                           default:s.wMilliseconds=vv;}
                chk(&s);
            }
            static const unsigned short odd[]={0,1,0x7FFE,0x7FFF,0x8000,0x8001,0xFFFE,0xFFFF,1600,1601,30827,30828};
            for(unsigned i=0;i<sizeof(odd)/sizeof(odd[0]);++i){
                WIA_ST s={2023,6,3,15,13,45,7,123};
                switch(fi){case 0:s.wYear=odd[i];break;case 1:s.wMonth=odd[i];break;case 2:s.wDay=odd[i];break;
                           case 3:s.wHour=odd[i];break;case 4:s.wMinute=odd[i];break;case 5:s.wSecond=odd[i];break;
                           default:s.wMilliseconds=odd[i];}
                chk(&s);
            }
        }
    }
    printf("after field edges: n=%lld failures=%d\n", n, fails);

    // ---- all 65536 wDayOfWeek values on one valid date ----
    for(long dw=0; dw<=65535 && fails<20; ++dw){ WIA_ST s={2023,6,(unsigned short)dw,15,13,45,7,123}; chk(&s); }
    printf("after wDayOfWeek 0..65535: n=%lld failures=%d\n", n, fails);

    // ---- fuzz ----
    { unsigned long long sd=0x243f6a8885a308d3ULL;
      for(int i=0;i<3000000 && fails<20;++i){ sd=sd*6364136223846793005ULL+1442695040888963407ULL;
        WIA_ST s; s.wYear=(unsigned short)((sd>>3)%40000); s.wMonth=(unsigned short)((sd>>19)%16);
        s.wDayOfWeek=(unsigned short)(sd>>13); s.wDay=(unsigned short)((sd>>23)%34);
        s.wHour=(unsigned short)((sd>>29)%26); s.wMinute=(unsigned short)((sd>>35)%63);
        s.wSecond=(unsigned short)((sd>>41)%63); s.wMilliseconds=(unsigned short)((sd>>47)%1024);
        chk(&s); }
      // and a second fuzz with completely unconstrained 16-bit fields
      for(int i=0;i<3000000 && fails<20;++i){ sd=sd*6364136223846793005ULL+1442695040888963407ULL;
        WIA_ST s; s.wYear=(unsigned short)(sd>>1); s.wMonth=(unsigned short)(sd>>17);
        s.wDayOfWeek=(unsigned short)(sd>>33); s.wDay=(unsigned short)(sd>>49);
        s.wHour=(unsigned short)(sd>>5); s.wMinute=(unsigned short)(sd>>21);
        s.wSecond=(unsigned short)(sd>>37); s.wMilliseconds=(unsigned short)(sd>>53);
        chk(&s); }
    }
    printf("\nREFERENCE vs LIVE: %s   cases=%lld failures=%d\n", fails?"FAIL":"PASS", n, fails);
    return fails?1:0;
}
