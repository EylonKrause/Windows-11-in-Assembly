// changes/293-systemtimetofiletime/probes/leap_enabled.c
// Probe 5. The one open question left by probe 2.
//
// ntdll!RtlpTimeFieldsToTimeEx takes its leap-second-aware body when PEB->LeapSecondData is
// non-NULL with Enabled != 0 (true on this machine) and then branches on PEB->LeapSecondFlags
// bit 0, SixtySecondEnabled, which is 0 for this process. So the whole leap-second feature is
// inert here -- but the flag is PER PROCESS, and a process that opts in (manifest, or
// NtSetInformationProcess(ProcessLeapSecondInfo)) would get different behaviour from the same
// export. Before deciding whether impl.asm needs a guard for that, find out WHAT actually
// changes: is it only wSecond == 60, or does any already-valid input move?
//
// The flag lives in this process's own PEB, which is writable, and ntdll reads it on every call
// (mov rbx,gs:[60h] / mov ebx,[rbx+7C0h] / and ebx,1). So set it, re-run the comparison against
// plain Gregorian arithmetic, and put it back.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <intrin.h>

typedef BOOL (WINAPI *fnST2FT)(const SYSTEMTIME*, LPFILETIME);
static fnST2FT sys;
static BYTE* peb;
#define LSDATA  (*(BYTE**)(peb+0x7B8))
#define LSFLAGS (*(ULONG*)(peb+0x7C0))

static int leapy(int y){ return (y%4==0 && y%100!=0) || y%400==0; }
static int dim(int y,int m){ static const int D[13]={0,31,28,31,30,31,30,31,31,30,31,30,31};
    if(m==2 && leapy(y)) return 29; return (m>=1&&m<=12)?D[m]:0; }
static int plain(const SYSTEMTIME* s, ULONGLONG* out){
    int y=(short)s->wYear,m=(short)s->wMonth,d=(short)s->wDay;
    if(m<1||m>12) return 0;
    if(d<1||d>dim(y,m)) return 0;
    if((short)s->wHour<0||(short)s->wHour>23) return 0;
    if((short)s->wMinute<0||(short)s->wMinute>59) return 0;
    if((short)s->wSecond<0||(short)s->wSecond>59) return 0;
    if((short)s->wMilliseconds<0||(short)s->wMilliseconds>999) return 0;
    if(y<1601||y>30827) return 0;
    long long yy=y; yy-=(m<=2); long long era=yy/400, yoe=yy-era*400;
    long long doy=(153*(m+(m>2?-3:9))+2)/5+d-1;
    long long doe=yoe*365+yoe/4-yoe/100+doy;
    long long days=era*146097+doe-584694;
    *out=(ULONGLONG)(days*864000000000LL + (long long)(short)s->wHour*36000000000LL
        + (long long)(short)s->wMinute*600000000LL + (long long)(short)s->wSecond*10000000LL
        + (long long)(short)s->wMilliseconds*10000LL);
    return 1;
}
static ULONGLONG ft2u(FILETIME f){ return ((ULONGLONG)f.dwHighDateTime<<32)|f.dwLowDateTime; }

static void sweep(const char* tag){
    long long ok=0, diff=0; int shown=0;
    unsigned long long sd=0x243f6a8885a308d3ULL;
    /* the whole domain at a stride, every field edge, and 3M fuzz over the full 16-bit range */
    for(int y=1601;y<=30827;y+=1)
        for(int m=1;m<=12;++m){
            int dd=dim(y,m);
            for(int d=1; d<=dd; d += (dd-1>0?dd-1:1)){
                for(int se=58; se<=61; ++se){
                    SYSTEMTIME s={(WORD)y,(WORD)m,0,(WORD)d,23,59,(WORD)se,999};
                    FILETIME f; *(ULONGLONG*)&f=0x5A5A5A5A5A5A5A5AULL; ULONGLONG r=0;
                    BOOL a=sys(&s,&f); int b=plain(&s,&r);
                    if((!!a)!=(!!b) || (a && ft2u(f)!=r)){
                        if(shown<8){ printf("   [%s] DIFF %d-%02d-%02d 23:59:%02d.999  live{%d,%llu} plain{%d,%llu}\n",
                                            tag,y,m,d,se,a,ft2u(f),b,r); ++shown; }
                        ++diff;
                    } else ++ok;
                }
            }
        }
    for(int i=0;i<3000000;++i){
        sd=sd*6364136223846793005ULL+1442695040888963407ULL;
        SYSTEMTIME s;
        s.wYear=(WORD)((sd>>3)%40000); s.wMonth=(WORD)((sd>>19)%16); s.wDayOfWeek=(WORD)(sd>>13);
        s.wDay=(WORD)((sd>>23)%34); s.wHour=(WORD)((sd>>29)%26); s.wMinute=(WORD)((sd>>35)%63);
        s.wSecond=(WORD)((sd>>41)%63); s.wMilliseconds=(WORD)((sd>>47)%1024);
        FILETIME f; *(ULONGLONG*)&f=0x5A5A5A5A5A5A5A5AULL; ULONGLONG r=0;
        BOOL a=sys(&s,&f); int b=plain(&s,&r);
        if((!!a)!=(!!b) || (a && ft2u(f)!=r)){
            if(shown<8){ printf("   [%s] FUZZ DIFF %u-%02u-%02u %02u:%02u:%02u.%03u live{%d,%llu} plain{%d,%llu}\n",
                                tag,s.wYear,s.wMonth,s.wDay,s.wHour,s.wMinute,s.wSecond,s.wMilliseconds,a,ft2u(f),b,r); ++shown; }
            ++diff;
        } else ++ok;
    }
    printf("  [%s] agree=%lld  differ=%lld\n", tag, ok, diff);
}

int main(void){
    sys=(fnST2FT)GetProcAddress(LoadLibraryW(L"kernel32.dll"),"SystemTimeToFileTime");
    peb=(BYTE*)__readgsqword(0x60);
    printf("LeapSecondData=%p Enabled=%d Count=%lu  LeapSecondFlags=0x%lX\n",
           (void*)LSDATA, LSDATA?*LSDATA:-1, LSDATA?*(ULONG*)(LSDATA+4):0, (unsigned long)LSFLAGS);

    printf("\nAS SHIPPED (SixtySecondEnabled = 0):\n");
    sweep("off");

    ULONG save = LSFLAGS;
    LSFLAGS |= 1;                                     /* opt this process into the 60th second */
    printf("\nWITH PEB->LeapSecondFlags bit 0 SET (SixtySecondEnabled = 1):\n");
    printf("  spot check: 2016-12-31 23:59:60.000 -> ");
    { SYSTEMTIME s={2016,12,0,31,23,59,60,0}; FILETIME f; *(ULONGLONG*)&f=0;
      BOOL a=sys(&s,&f); printf("r=%d t=%llu\n", a, ft2u(f)); }
    printf("  spot check: 2023-06-15 13:45:60.123 -> ");
    { SYSTEMTIME s={2023,6,0,15,13,45,60,123}; FILETIME f; *(ULONGLONG*)&f=0;
      BOOL a=sys(&s,&f); printf("r=%d t=%llu\n", a, ft2u(f)); }
    printf("  spot check: 2023-06-15 13:45:61.123 -> ");
    { SYSTEMTIME s={2023,6,0,15,13,45,61,123}; FILETIME f; *(ULONGLONG*)&f=0;
      BOOL a=sys(&s,&f); printf("r=%d t=%llu\n", a, ft2u(f)); }
    sweep("on");
    LSFLAGS = save;
    printf("\nrestored LeapSecondFlags=0x%lX\n", (unsigned long)LSFLAGS);
    return 0;
}
