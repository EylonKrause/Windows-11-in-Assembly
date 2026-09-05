// changes/127-rtltimefieldstotime/correctness.c
// Bit-exact check of wia_fields2time vs live ntdll!RtlTimeFieldsToTime + oracle: BOOLEAN return AND
// the written time, over an 800-year day-by-day sweep, every field's range edges (incl. negatives and
// the year-30828 cutoff), a full round-trip against RtlTimeToTimeFields, and 3M fuzz.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
typedef struct { short Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday; } WIA_TF;
extern unsigned char wia_fields2time(const WIA_TF*, long long*);
unsigned char ref_fields2time(const WIA_TF*, long long*);
typedef unsigned char (WINAPI *fn)(WIA_TF*, long long*);
typedef void (WINAPI *fnT2F)(long long*, WIA_TF*);
static fn sys; static fnT2F sysT2F;
static int fails=0;
static void chk(WIA_TF* tf){
    long long a=0x5A5A5A5A5A5A5A5ALL, b=0x3C3C3C3C3C3C3C3CLL, c=0x7777777777777777LL;
    unsigned char r1=sys(tf,&a), r2=wia_fields2time(tf,&b), r3=ref_fields2time(tf,&c);
    int ok=(r1==r2)&&(r1==r3);
    if(r1 && (a!=b || a!=c)) ok=0;
    if(!ok){ if(fails<25) printf("FAIL %d-%d-%d %d:%02d:%02d.%03d w%d sys{%d,%lld} ours{%d,%lld} ref{%d,%lld}\n",
        tf->Year,tf->Month,tf->Day,tf->Hour,tf->Minute,tf->Second,tf->Milliseconds,tf->Weekday,r1,a,r2,b,r3,c); ++fails; }
}
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    sys=(fn)GetProcAddress(h,"RtlTimeFieldsToTime");
    sysT2F=(fnT2F)GetProcAddress(h,"RtlTimeToTimeFields");
    if(!sys||!sysT2F){ printf("missing export\n"); return 2; }
    WIA_TF t;
    for(int y=1601;y<=2400 && fails<25;y++) for(int m=1;m<=12;m++) for(int d=1;d<=31;d++){
        t.Year=(short)y;t.Month=(short)m;t.Day=(short)d;t.Hour=13;t.Minute=45;t.Second=7;t.Milliseconds=123;t.Weekday=0; chk(&t); }
    for(int m=-2;m<=14 && fails<25;m++) for(int d=-2;d<=33;d++){
        t.Year=2023;t.Month=(short)m;t.Day=(short)d;t.Hour=0;t.Minute=0;t.Second=0;t.Milliseconds=0;t.Weekday=3; chk(&t); }
    for(int v=-2;v<=25 && fails<25;v++){ t.Year=2023;t.Month=6;t.Day=15;t.Hour=(short)v;t.Minute=0;t.Second=0;t.Milliseconds=0; chk(&t); }
    for(int v=-2;v<=61 && fails<25;v++){ t.Year=2023;t.Month=6;t.Day=15;t.Hour=1;t.Minute=(short)v;t.Second=0;t.Milliseconds=0; chk(&t); }
    for(int v=-2;v<=61 && fails<25;v++){ t.Year=2023;t.Month=6;t.Day=15;t.Hour=1;t.Minute=1;t.Second=(short)v;t.Milliseconds=0; chk(&t); }
    for(int v=-2;v<=1001 && fails<25;v++){ t.Year=2023;t.Month=6;t.Day=15;t.Hour=1;t.Minute=1;t.Second=1;t.Milliseconds=(short)v; chk(&t); }
    for(int y=-5;y<=1650 && fails<25;y++){ t.Year=(short)y;t.Month=1;t.Day=1;t.Hour=0;t.Minute=0;t.Second=0;t.Milliseconds=0; chk(&t); }
    for(int y=30800;y<=30835 && fails<25;y++){ t.Year=(short)y;t.Month=1;t.Day=1;t.Hour=0;t.Minute=0;t.Second=0;t.Milliseconds=0; chk(&t); }
    // round-trip: every day of the domain -> fields -> back, must reproduce the instant exactly
    for(long long d=0; d<10600000 && fails<25; d+=97){
        long long tt=d*864000000000LL + 12345670000LL; WIA_TF f; sysT2F(&tt,&f);
        long long back=0; if(wia_fields2time(&f,&back) && back!=tt){
            if(fails<25) printf("ROUNDTRIP FAIL day=%lld t=%lld back=%lld\n",d,tt,back); ++fails; }
        chk(&f);
    }
    unsigned long long s=0x243f6a8885a308d3ULL;
    for(int i=0;i<3000000 && fails<25;i++){
        s=s*6364136223846793005ULL+1442695040888963407ULL;
        t.Year=(short)((s>>3)%32768); t.Month=(short)((s>>19)%16); t.Day=(short)((s>>23)%34);
        t.Hour=(short)((s>>29)%26); t.Minute=(short)((s>>35)%63); t.Second=(short)((s>>41)%63);
        t.Milliseconds=(short)((s>>47)%1024); t.Weekday=(short)((s>>13)%9);
        chk(&t);
    }
    if(!fails) printf("CORRECTNESS: PASS (RtlTimeFieldsToTime vs live + oracle: 800y sweep, all range edges, 109k round-trips, 3M fuzz)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
