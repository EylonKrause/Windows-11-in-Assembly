// changes/128-rtlsecondssince1970totime/reference.c
// Oracle for ntdll!RtlSecondsSince1970ToTime.
void ref_secs2time(unsigned long secs, long long* out){
    *out = ((long long)secs + 11644473600LL) * 10000000LL;
}
