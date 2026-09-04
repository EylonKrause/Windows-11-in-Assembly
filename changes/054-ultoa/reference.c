// changes/054-ultoa/reference.c — scalar oracle for _ultoa (radix 2..36, lowercase a-z).
char* ref_ultoa(unsigned long v, char* s, int radix){
    char tmp[40]; int n=0;
    if(v==0) tmp[n++]='0';
    else while(v){ unsigned d=v%(unsigned)radix; v/=(unsigned)radix; tmp[n++]=(char)(d<10 ? '0'+d : 'a'+d-10); }
    int i; for(i=0;i<n;i++) s[i]=tmp[n-1-i];
    s[n]=0;
    return s;
}
