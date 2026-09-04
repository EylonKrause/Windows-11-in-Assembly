// changes/057-i64toa/reference.c — scalar oracle for _i64toa (radix 10 signed; other radix unsigned).
char* ref_i64toa(long long value, char* s, int radix){
    char* p=s; unsigned long long v;
    if(radix==10 && value<0){ *p++='-'; v=0ull-(unsigned long long)value; } else v=(unsigned long long)value;
    char tmp[72]; int n=0;
    if(v==0) tmp[n++]='0';
    else while(v){ unsigned d=(unsigned)(v%(unsigned)radix); v/=(unsigned)radix; tmp[n++]=(char)(d<10?'0'+d:'a'+d-10); }
    for(int i=0;i<n;i++) p[i]=tmp[n-1-i];
    p[n]=0; return s;
}
