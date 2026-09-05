// changes/127-rtltimefieldstotime/reference.c
// Oracle for ntdll!RtlTimeFieldsToTime, validated bit-exact vs the live export over an 800-year
// day-by-day sweep, every field range edge, and 3M fuzz. Era-based days-from-civil.
typedef struct { short Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday; } WIA_TF;
static int wia_leap(int y){ return (y%4==0 && y%100!=0) || y%400==0; }
static int wia_dim(int y,int m){ static const int D[13]={0,31,28,31,30,31,30,31,31,30,31,30,31};
    if(m==2 && wia_leap(y)) return 29; return (m>=1&&m<=12)?D[m]:0; }
unsigned char ref_fields2time(const WIA_TF* tf, long long* out){
    int y=tf->Year, m=tf->Month, d=tf->Day;
    if(m<1||m>12) return 0;
    if(d<1||d>wia_dim(y,m)) return 0;
    if(tf->Hour<0||tf->Hour>23) return 0;
    if(tf->Minute<0||tf->Minute>59) return 0;
    if(tf->Second<0||tf->Second>59) return 0;
    if(tf->Milliseconds<0||tf->Milliseconds>999) return 0;
    if(y<1601 || y>30827) return 0;               // ntdll rejects year 30828+ outright
    long long yy=y; yy -= (m<=2);
    long long era=yy/400;
    long long yoe=yy-era*400;
    long long doy=(153*(m + (m>2?-3:9)) + 2)/5 + d-1;
    long long doe=yoe*365 + yoe/4 - yoe/100 + doy;
    long long days=era*146097 + doe - 584694;     // 719468 - 134774
    *out = days*864000000000LL + (long long)tf->Hour*36000000000LL
         + (long long)tf->Minute*600000000LL + (long long)tf->Second*10000000LL
         + (long long)tf->Milliseconds*10000LL;
    return 1;
}
