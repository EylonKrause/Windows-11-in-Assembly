// changes/055-ui64toa/reference.c — scalar oracle for _ui64toa (radix 2..36, lowercase).
char* ref_ui64toa(unsigned long long v, char* s, int radix){
    char tmp[72]; int n=0;
    if(v==0) tmp[n++]='0';
    else while(v){ unsigned d=(unsigned)(v%(unsigned)radix); v/=(unsigned)radix; tmp[n++]=(char)(d<10 ? '0'+d : 'a'+d-10); }
    for(int i=0;i<n;i++) s[i]=tmp[n-1-i];
    s[n]=0; return s;
}
