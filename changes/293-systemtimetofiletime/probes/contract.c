// changes/293-systemtimetofiletime/probes/contract.c
// Throwaway probe. PINS the observable contract of the LIVE kernel32!SystemTimeToFileTime on this
// machine before a line of reference.c or impl.asm is written. Nothing here is assumed from MSDN.
//
// Questions asked:
//   Q1  Is PEB->LeapSecondData enabled for this process?  (ntdll takes a different path if so)
//   Q2  Which SYSTEMTIME fields are read at all?  (is wDayOfWeek ignored on input?)
//   Q3  What are the year bounds, both ends?
//   Q4  Which invalid field produces which return / GetLastError / RtlGetLastNtStatus?
//   Q5  Is *lpFileTime written on failure?
//   Q6  Is the last error touched on SUCCESS?
//   Q7  Is wSecond == 60 accepted (leap second)?
//   Q8  How are WORD values above 0x7FFF treated (signed CSHORT wrap)?
//   Q9  Is the day-of-month checked against the ACTUAL month length, with real Gregorian leap rules?
//   Q10 Does the epoch/scale match days*864e9 + h*36e9 + m*6e8 + s*1e7 + ms*1e4 ?
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef BOOL (WINAPI *fnST2FT)(const SYSTEMTIME*, LPFILETIME);
typedef LONG (WINAPI *fnGetLastNtStatus)(void);
static fnST2FT sys;
static fnGetLastNtStatus getst;

#define SENT 0x5A5A5A5A5A5A5A5AULL

static ULONGLONG ft2u(FILETIME f){ return ((ULONGLONG)f.dwHighDateTime<<32)|f.dwLowDateTime; }

typedef struct { BOOL r; DWORD err; LONG st; ULONGLONG t; BOOL written; } RES;

static RES call(const SYSTEMTIME* s){
    RES o; FILETIME f;
    *(ULONGLONG*)&f = SENT;
    SetLastError(0xD1CED1CE);
    o.r = sys(s,&f);
    o.err = GetLastError();
    o.st  = getst ? getst() : 0;
    o.t = ft2u(f);
    o.written = (o.t != SENT);
    return o;
}

static SYSTEMTIME mk(int y,int mo,int dow,int d,int h,int mi,int se,int ms){
    SYSTEMTIME s; s.wYear=(WORD)y; s.wMonth=(WORD)mo; s.wDayOfWeek=(WORD)dow; s.wDay=(WORD)d;
    s.wHour=(WORD)h; s.wMinute=(WORD)mi; s.wSecond=(WORD)se; s.wMilliseconds=(WORD)ms; return s;
}

static void show(const char* tag, SYSTEMTIME s){
    RES o = call(&s);
    printf("%-46s -> r=%d err=%lu st=0x%08lX written=%d t=%llu\n",
           tag, o.r, (unsigned long)o.err, (unsigned long)o.st, o.written, o.t);
}

int main(void){
    HMODULE k32 = LoadLibraryW(L"kernel32.dll");
    HMODULE ntd = LoadLibraryW(L"ntdll.dll");
    sys   = (fnST2FT)GetProcAddress(k32,"SystemTimeToFileTime");
    getst = (fnGetLastNtStatus)GetProcAddress(ntd,"RtlGetLastNtStatus");
    if(!sys){ printf("no export\n"); return 2; }

    // ---- Q1: PEB->LeapSecondData (x64 PEB offset 0x7B8) + LeapSecondFlags (0x7C0) ----
    {
        BYTE* peb = (BYTE*)__readgsqword(0x60);
        void* lsd = *(void**)(peb+0x7B8);
        ULONG flags = *(ULONG*)(peb+0x7C0);
        printf("Q1  PEB=%p  LeapSecondData=%p  enabled_byte=%d  LeapSecondFlags=0x%lX\n",
               (void*)peb, lsd, lsd? *(BYTE*)lsd : -1, (unsigned long)flags);
    }

    // ---- Q2: wDayOfWeek ----
    printf("\nQ2  wDayOfWeek on a valid date (2023-06-15 13:45:07.123):\n");
    {
        ULONGLONG base=0; int diff=0;
        for(int dow=0; dow<=9; ++dow){
            SYSTEMTIME s = mk(2023,6,dow,15,13,45,7,123);
            RES o = call(&s);
            if(dow==0) base=o.t; else if(o.t!=base||!o.r) diff=1;
        }
        { SYSTEMTIME s=mk(2023,6,0xFFFF,15,13,45,7,123); RES o=call(&s); if(o.t!=base||!o.r) diff=1; }
        { SYSTEMTIME s=mk(2023,6,0x7FFF,15,13,45,7,123); RES o=call(&s); if(o.t!=base||!o.r) diff=1; }
        printf("    dow in {0..9, 0x7FFF, 0xFFFF} all identical: %s (t=%llu)\n", diff?"NO":"YES", base);
    }

    // ---- Q3: year bounds ----
    printf("\nQ3  year bounds (Jan 1 00:00:00.000):\n");
    for(int y=1599;y<=1602;++y){ char b[64]; sprintf(b,"    year %d",y); show(b, mk(y,1,0,1,0,0,0,0)); }
    for(int y=30826;y<=30829;++y){ char b[64]; sprintf(b,"    year %d",y); show(b, mk(y,1,0,1,0,0,0,0)); }
    show("    year 0",       mk(0,1,0,1,0,0,0,0));
    show("    year 65535",   mk(65535,1,0,1,0,0,0,0));

    // ---- Q4/Q5: each invalid field ----
    printf("\nQ4/Q5  invalid fields (base 2023-06-15 13:45:07.123):\n");
    show("    valid baseline",          mk(2023, 6,0,15,13,45, 7,123));
    show("    wMonth=0",                mk(2023, 0,0,15,13,45, 7,123));
    show("    wMonth=13",               mk(2023,13,0,15,13,45, 7,123));
    show("    wMonth=0xFFFF",           mk(2023,0xFFFF,0,15,13,45,7,123));
    show("    wDay=0",                  mk(2023, 6,0, 0,13,45, 7,123));
    show("    wDay=31 in June",         mk(2023, 6,0,31,13,45, 7,123));
    show("    wDay=30 in June (ok)",    mk(2023, 6,0,30,13,45, 7,123));
    show("    wHour=24",                mk(2023, 6,0,15,24,45, 7,123));
    show("    wMinute=60",              mk(2023, 6,0,15,13,60, 7,123));
    show("    wSecond=60",              mk(2023, 6,0,15,13,45,60,123));
    show("    wSecond=61",              mk(2023, 6,0,15,13,45,61,123));
    show("    wMilliseconds=1000",      mk(2023, 6,0,15,13,45, 7,1000));
    show("    wMilliseconds=999 (ok)",  mk(2023, 6,0,15,13,45, 7,999));

    // ---- Q6: last error on success ----
    printf("\nQ6  last error across a SUCCESS:\n");
    {
        SYSTEMTIME s = mk(2023,6,0,15,13,45,7,123); FILETIME f;
        SetLastError(12345); BOOL r = sys(&s,&f);
        printf("    preset 12345 -> r=%d GetLastError=%lu\n", r, (unsigned long)GetLastError());
        SetLastError(0);     r = sys(&s,&f);
        printf("    preset 0     -> r=%d GetLastError=%lu\n", r, (unsigned long)GetLastError());
    }

    // ---- Q8: CSHORT wrap ----
    printf("\nQ8  WORD values above 0x7FFF (should wrap to negative CSHORT and be rejected):\n");
    show("    wHour=0x8000",            mk(2023, 6,0,15,0x8000,45,7,123));
    show("    wDay=0x8000",             mk(2023, 6,0,0x8000,13,45,7,123));
    show("    wMilliseconds=0x8000",    mk(2023, 6,0,15,13,45,7,0x8000));
    show("    wYear=0x8000",            mk(0x8000,6,0,15,13,45,7,123));

    // ---- Q9: month length + leap rules ----
    printf("\nQ9  day-of-month vs real month length (Feb 29 / Feb 30):\n");
    {
        int yy[] = {1600,1700,1800,1900,1996,2000,2004,2100,2200,2400,30824,30827};
        for(unsigned i=0;i<sizeof(yy)/sizeof(yy[0]);++i){
            SYSTEMTIME a = mk(yy[i],2,0,29,0,0,0,0);
            SYSTEMTIME b = mk(yy[i],2,0,28,0,0,0,0);
            RES ra = call(&a), rb = call(&b);
            printf("    %5d: Feb28 r=%d  Feb29 r=%d   (leap by rule: %d)\n",
                   yy[i], rb.r, ra.r, (yy[i]%4==0 && yy[i]%100!=0)||yy[i]%400==0);
        }
        SYSTEMTIME c = mk(2024,2,0,30,0,0,0,0); RES rc=call(&c);
        printf("    2024 Feb30 r=%d\n", rc.r);
        // every month's last valid day and first invalid day, 2023
        for(int m=1;m<=12;++m){
            int last=0; for(int d=1;d<=32;++d){ SYSTEMTIME s=mk(2023,m,0,d,0,0,0,0); if(call(&s).r) last=d; }
            printf("    2023-%02d last accepted day = %d\n", m, last);
        }
    }

    // ---- Q10: epoch + scale ----
    printf("\nQ10 epoch / scale:\n");
    {
        struct { int y,mo,d,h,mi,s,ms; ULONGLONG want; } t[] = {
            {1601, 1, 1, 0, 0, 0,   0, 0ULL},
            {1601, 1, 1, 0, 0, 0,   1, 10000ULL},
            {1601, 1, 1, 0, 0, 1,   0, 10000000ULL},
            {1601, 1, 1, 0, 1, 0,   0, 600000000ULL},
            {1601, 1, 1, 1, 0, 0,   0, 36000000000ULL},
            {1601, 1, 2, 0, 0, 0,   0, 864000000000ULL},
            {1970, 1, 1, 0, 0, 0,   0, 116444736000000000ULL},
            {30827,12,31,23,59,59,999, 0ULL},
        };
        for(unsigned i=0;i<sizeof(t)/sizeof(t[0]);++i){
            SYSTEMTIME s = mk(t[i].y,t[i].mo,0,t[i].d,t[i].h,t[i].mi,t[i].s,t[i].ms);
            RES o = call(&s);
            printf("    %5d-%02d-%02d %02d:%02d:%02d.%03d -> r=%d t=%llu  want=%llu %s\n",
                   t[i].y,t[i].mo,t[i].d,t[i].h,t[i].mi,t[i].s,t[i].ms, o.r, o.t, t[i].want,
                   (i+1<sizeof(t)/sizeof(t[0]) && o.t==t[i].want)?"OK":"");
        }
    }

    // ---- extra: does it match ntdll!RtlTimeFieldsToTime exactly (Weekday forced 0)? ----
    printf("\nQ11 vs ntdll!RtlTimeFieldsToTime directly, 200k random field sets:\n");
    {
        typedef BOOLEAN (WINAPI *fnTF)(void*, LONGLONG*);
        fnTF tf = (fnTF)GetProcAddress(ntd,"RtlTimeFieldsToTime");
        unsigned long long seed=0x243f6a8885a308d3ULL; int mism=0, agree=0;
        for(int i=0;i<200000;++i){
            seed = seed*6364136223846793005ULL + 1442695040888963407ULL;
            SYSTEMTIME s;
            s.wYear=(WORD)((seed>>3)%40000); s.wMonth=(WORD)((seed>>19)%16);
            s.wDayOfWeek=(WORD)((seed>>13)%9); s.wDay=(WORD)((seed>>23)%34);
            s.wHour=(WORD)((seed>>29)%26); s.wMinute=(WORD)((seed>>35)%63);
            s.wSecond=(WORD)((seed>>41)%63); s.wMilliseconds=(WORD)((seed>>47)%1024);
            short tfv[8] = {(short)s.wYear,(short)s.wMonth,(short)s.wDay,(short)s.wHour,
                            (short)s.wMinute,(short)s.wSecond,(short)s.wMilliseconds,0};
            LONGLONG a=0; BOOLEAN ra = tf(tfv,&a);
            FILETIME f; *(ULONGLONG*)&f = SENT; BOOL rb = sys(&s,&f);
            ULONGLONG b = ft2u(f);
            if((!!ra)!=(!!rb) || (ra && a!=(LONGLONG)b) || (!ra && b!=SENT)) { if(mism<5)
                printf("    MISMATCH y=%u m=%u d=%u ntdll{%d,%lld} k32{%d,%llu}\n",
                       s.wYear,s.wMonth,s.wDay,ra,a,rb,b); ++mism; }
            else ++agree;
        }
        printf("    agree=%d mismatch=%d\n", agree, mism);
    }
    return 0;
}
