// changes/126-rtltimetotimefields/reference.c
// Oracle for ntdll!RtlTimeToTimeFields (Time >= 0), validated bit-exact vs the live export over every
// day boundary of the whole domain (10.67M) + 3M full-range fuzz. Era-based civil-from-days.
typedef struct { short Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday; } WIA_TF;
#define TPD 864000000000LL
#define TPH 36000000000LL
#define TPM 600000000LL
#define TPS 10000000LL
#define TPMS 10000LL
void ref_time2fields(const long long* pt, WIA_TF* tf){
    long long t=*pt;
    long long days = t / TPD, rem = t % TPD;
    tf->Hour         = (short)(rem / TPH);
    tf->Minute       = (short)((rem / TPM) % 60);
    tf->Second       = (short)((rem / TPS) % 60);
    tf->Milliseconds = (short)((rem / TPMS) % 1000);
    tf->Weekday      = (short)((days + 1) % 7);       // 1601-01-01 was a Monday
    long long z   = days - 134774 + 719468;
    long long era = z / 146097;
    long long doe = z - era * 146097;
    long long yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    long long y   = yoe + era * 400;
    long long doy = doe - (365*yoe + yoe/4 - yoe/100);
    long long mp  = (5*doy + 2)/153;
    long long d   = doy - (153*mp + 2)/5 + 1;
    long long m   = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
    tf->Year=(short)y; tf->Month=(short)m; tf->Day=(short)d;
}
