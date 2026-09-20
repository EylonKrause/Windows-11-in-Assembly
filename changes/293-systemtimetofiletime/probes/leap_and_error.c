// changes/293-systemtimetofiletime/probes/leap_and_error.c
// Probe 2. Three things the first probe left open:
//
//  A. PEB->LeapSecondData is NON-NULL and its Enabled byte is 1 on this machine, so ntdll's
//     RtlTimeFieldsToTime does NOT take its tail-jump to the plain no-leap core. It runs the
//     leap-second-aware body, which begins with a `lock or dword ptr [rsp],esi` fence. Does the
//     OUTPUT differ from plain era arithmetic anywhere in the domain, or is it only the second-60
//     acceptance that changes (gated on PEB->LeapSecondFlags bit 0, which reads 0 here)?
//
//  B. Exactly which TEB fields the failure path writes: LastErrorValue (TEB+0x68) and
//     LastStatusValue (TEB+0x1250). BaseSetLastNTError -> RtlNtStatusToDosError writes both.
//     If our impl is to be indistinguishable it must write both, and must write NEITHER on success.
//
//  C. The headroom: time the live export against a plain /O2 C version of change 127's arithmetic,
//     so we know before writing any assembly whether there is a win here at all.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <intrin.h>

typedef BOOL (WINAPI *fnST2FT)(const SYSTEMTIME*, LPFILETIME);
static fnST2FT k32fn, kbfn;

static BYTE* teb(void){ return (BYTE*)__readgsqword(0x30); }
static BYTE* peb(void){ return (BYTE*)__readgsqword(0x60); }
#define LASTERR  (*(DWORD*)(teb()+0x68))
#define LASTSTAT (*(LONG *)(teb()+0x1250))

// ---- plain era arithmetic, change 127's oracle, no leap seconds anywhere ----
static int leapy(int y){ return (y%4==0 && y%100!=0) || y%400==0; }
static int dim(int y,int m){ static const int D[13]={0,31,28,31,30,31,30,31,31,30,31,30,31};
    if(m==2 && leapy(y)) return 29; return (m>=1&&m<=12)?D[m]:0; }
static int ref_st2ft(const SYSTEMTIME* s, ULONGLONG* out){
    int y=(short)s->wYear, m=(short)s->wMonth, d=(short)s->wDay;
    if(m<1||m>12) return 0;
    if(d<1||d>dim(y,m)) return 0;
    if((short)s->wHour<0||(short)s->wHour>23) return 0;
    if((short)s->wMinute<0||(short)s->wMinute>59) return 0;
    if((short)s->wSecond<0||(short)s->wSecond>59) return 0;
    if((short)s->wMilliseconds<0||(short)s->wMilliseconds>999) return 0;
    if(y<1601||y>30827) return 0;
    long long yy=y; yy -= (m<=2);
    long long era=yy/400, yoe=yy-era*400;
    long long doy=(153*(m + (m>2?-3:9)) + 2)/5 + d-1;
    long long doe=yoe*365 + yoe/4 - yoe/100 + doy;
    long long days=era*146097 + doe - 584694;
    *out = (ULONGLONG)(days*864000000000LL + (long long)(short)s->wHour*36000000000LL
         + (long long)(short)s->wMinute*600000000LL + (long long)(short)s->wSecond*10000000LL
         + (long long)(short)s->wMilliseconds*10000LL);
    return 1;
}

static ULONGLONG ft2u(FILETIME f){ return ((ULONGLONG)f.dwHighDateTime<<32)|f.dwLowDateTime; }

int main(void){
    HMODULE k32=LoadLibraryW(L"kernel32.dll"), kb=LoadLibraryW(L"kernelbase.dll");
    k32fn=(fnST2FT)GetProcAddress(k32,"SystemTimeToFileTime");
    kbfn =(fnST2FT)GetProcAddress(kb ,"SystemTimeToFileTime");
    printf("kernel32 fn=%p  kernelbase fn=%p\n",(void*)k32fn,(void*)kbfn);

    // ---------------- A. leap-second state + does the OUTPUT ever differ ----------------
    {
        void* lsd = *(void**)(peb()+0x7B8);
        ULONG fl  = *(ULONG*)(peb()+0x7C0);
        printf("\nA. LeapSecondData=%p Enabled=%d LeapSecondFlags=0x%lX (bit0 SixtySecondEnabled=%lu)\n",
               lsd, lsd?*(BYTE*)lsd:-1, (unsigned long)fl, (unsigned long)(fl&1));
        if(lsd){
            // RTL_LEAP_SECOND_DATA: BOOLEAN Enabled; ULONG Count; LARGE_INTEGER Data[N];
            ULONG cnt = *(ULONG*)((BYTE*)lsd+4);
            printf("   leap-second record count = %lu\n", (unsigned long)cnt);
            for(ULONG i=0;i<cnt && i<8;++i)
                printf("     [%lu] = %lld\n",(unsigned long)i, *(LONGLONG*)((BYTE*)lsd+8+8*i));
        }
        // full day-by-day sweep 1601..30827 at a stride, plus every second of a few days
        unsigned long long mism=0, ok=0;
        for(int y=1601;y<=30827;y+=1){
            for(int m=1;m<=12;++m){
                int dd = dim(y,m);
                for(int d=1; d<=dd; d += (dd-1>0?dd-1:1)){   // first and last day of each month
                    SYSTEMTIME s={(WORD)y,(WORD)m,0,(WORD)d,23,59,59,999};
                    FILETIME f; ULONGLONG r=0;
                    BOOL a=k32fn(&s,&f); int b=ref_st2ft(&s,&r);
                    if((!!a)!=(!!b) || (a && ft2u(f)!=r)){ if(mism<5) printf("   OUTPUT DIFF %d-%02d-%02d live=%llu ref=%llu\n",y,m,d,ft2u(f),r); ++mism; }
                    else ++ok;
                }
            }
        }
        // every second of 2016-12-31 and 2017-01-01 (a real leap-second boundary: 2016-12-31 23:59:60)
        for(int dsel=0; dsel<2; ++dsel)
            for(int h=0;h<24;++h) for(int mi=0;mi<60;++mi) for(int se=0;se<60;++se){
                SYSTEMTIME s={2016,12,0,(WORD)(31+dsel),(WORD)h,(WORD)mi,(WORD)se,500};
                if(dsel){ s.wYear=2017; s.wMonth=1; s.wDay=1; }
                FILETIME f; ULONGLONG r=0;
                BOOL a=k32fn(&s,&f); int b=ref_st2ft(&s,&r);
                if((!!a)!=(!!b) || (a && ft2u(f)!=r)){ if(mism<5) printf("   LEAPBOUND DIFF %u-%02u-%02u %02u:%02u:%02u live=%llu ref=%llu\n",s.wYear,s.wMonth,s.wDay,s.wHour,s.wMinute,s.wSecond,ft2u(f),r); ++mism; }
                else ++ok;
            }
        printf("   era-arithmetic vs live: ok=%llu mismatch=%llu\n", ok, mism);
    }

    // ---------------- B. TEB writes ----------------
    {
        printf("\nB. TEB writes\n");
        SYSTEMTIME good={2023,6,0,15,13,45,7,123}, bad={2023,13,0,15,13,45,7,123};
        FILETIME f;
        LASTERR=0x11111111; LASTSTAT=0x22222222;
        BOOL r=k32fn(&good,&f);
        printf("   SUCCESS: r=%d  LastErrorValue=0x%08lX  LastStatusValue=0x%08lX\n",
               r,(unsigned long)LASTERR,(unsigned long)LASTSTAT);
        LASTERR=0x11111111; LASTSTAT=0x22222222;
        r=k32fn(&bad,&f);
        printf("   FAILURE: r=%d  LastErrorValue=0x%08lX (%lu)  LastStatusValue=0x%08lX\n",
               r,(unsigned long)LASTERR,(unsigned long)LASTERR,(unsigned long)LASTSTAT);
        // sanity: is TEB+0x68 really what GetLastError reads?
        LASTERR=4242; printf("   GetLastError() after poking TEB+0x68=4242 -> %lu\n",(unsigned long)GetLastError());
        // sanity: is TEB+0x1250 really what RtlGetLastNtStatus reads?
        typedef LONG (WINAPI *fnS)(void); fnS gs=(fnS)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetLastNtStatus");
        LASTSTAT=(LONG)0xC0000999; printf("   RtlGetLastNtStatus() after poking TEB+0x1250 -> 0x%08lX\n",(unsigned long)(gs?gs():0));
    }

    // ---------------- C. headroom ----------------
    {
        printf("\nC. headroom (min of 200 batches, 20000 calls each, pinned core)\n");
        SetThreadAffinityMask(GetCurrentThread(),1<<2);
        SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS);
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_TIME_CRITICAL);
        LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
        static SYSTEMTIME S={2023,6,0,15,13,45,7,123};
        volatile ULONGLONG sink=0;
        const int INNER=20000, TR=200;
        double bk32=1e300,bkb=1e300,bref=1e300;
        for(int t=0;t<TR;++t){
            LARGE_INTEGER a,b; FILETIME f;
            QueryPerformanceCounter(&a); for(int i=0;i<INNER;++i){ k32fn(&S,&f); sink^=ft2u(f);} QueryPerformanceCounter(&b);
            double n=(double)(b.QuadPart-a.QuadPart)*1e9/fq.QuadPart/INNER; if(n<bk32) bk32=n;
            QueryPerformanceCounter(&a); for(int i=0;i<INNER;++i){ kbfn(&S,&f); sink^=ft2u(f);} QueryPerformanceCounter(&b);
            n=(double)(b.QuadPart-a.QuadPart)*1e9/fq.QuadPart/INNER; if(n<bkb) bkb=n;
            ULONGLONG r;
            QueryPerformanceCounter(&a); for(int i=0;i<INNER;++i){ ref_st2ft(&S,&r); sink^=r;} QueryPerformanceCounter(&b);
            n=(double)(b.QuadPart-a.QuadPart)*1e9/fq.QuadPart/INNER; if(n<bref) bref=n;
        }
        printf("   kernel32!SystemTimeToFileTime    %8.3f ns\n", bk32);
        printf("   kernelbase!SystemTimeToFileTime  %8.3f ns\n", bkb);
        printf("   plain C era arithmetic (/O2)     %8.3f ns   -> headroom %.2fx\n", bref, bk32/bref);
        printf("   sink=%llu\n",(unsigned long long)sink);
    }
    return 0;
}
