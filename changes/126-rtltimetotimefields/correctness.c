// changes/126-rtltimetotimefields/correctness.c
// Bit-exact check of wia_time2fields vs live ntdll!RtlTimeToTimeFields + oracle, over EVERY day
// boundary of the entire valid domain (10 675 199 days, 1601-01-01 .. year ~30828), all 8 TIME_FIELDS
// members, plus edges and 3M full-range random instants (sub-day resolution).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef struct { short Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday; } WIA_TF;
extern void wia_time2fields(const long long*, WIA_TF*);
void ref_time2fields(const long long*, WIA_TF*);
typedef void (WINAPI *fn)(long long*, WIA_TF*);
static fn sys;
static int fails=0;
#define TPD 864000000000LL
static void chk(long long t){
    WIA_TF a,b,c; memset(&a,0x5A,sizeof a); memset(&b,0x3C,sizeof b); memset(&c,0x77,sizeof c);
    sys(&t,&a); wia_time2fields(&t,&b); ref_time2fields(&t,&c);
    if(memcmp(&a,&b,sizeof a) || memcmp(&a,&c,sizeof a)){
        if(fails<20) printf("FAIL t=%lld sys{%d-%d-%d %d:%02d:%02d.%03d w%d} ours{%d-%d-%d %d:%02d:%02d.%03d w%d} ref{%d-%d-%d %d:%02d:%02d.%03d w%d}\n",
            t,a.Year,a.Month,a.Day,a.Hour,a.Minute,a.Second,a.Milliseconds,a.Weekday,
              b.Year,b.Month,b.Day,b.Hour,b.Minute,b.Second,b.Milliseconds,b.Weekday,
              c.Year,c.Month,c.Day,c.Hour,c.Minute,c.Second,c.Milliseconds,c.Weekday);
        ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlTimeToTimeFields");
    if(!sys){ printf("no RtlTimeToTimeFields\n"); return 2; }
    long long E[]={0,1,TPD-1,TPD,TPD*365,TPD*366,133200000000000000LL,9999999999999999LL,
        0x7FFFFFFFFFFFFFFFLL, TPD*134774, TPD*(134774+11322), TPD*145731, TPD*(365*400+97)};
    for(int i=0;i<(int)(sizeof(E)/sizeof(E[0]));i++) chk(E[i]);
    for(long long d=0; d<10675199 && fails<20; d++) chk(d*TPD);           // every day in the domain
    unsigned long long s=0x9e3779b97f4a7c15ULL;
    for(int i=0;i<3000000 && fails<20;i++){
        s = s*6364136223846793005ULL + 1442695040888963407ULL;
        chk((long long)(s % 0x7FFFFFFFFFFFFFFFULL));
    }
    if(!fails) printf("CORRECTNESS: PASS (RtlTimeToTimeFields vs live + oracle, all 8 fields: every one of 10.67M days + 3M full-range fuzz)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
