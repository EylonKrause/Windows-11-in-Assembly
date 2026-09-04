// changes/056-itoa/reference.c — scalar oracle for _itoa (radix 10 signed; other radix unsigned).
char* ref_itoa(int value, char* s, int radix){
    char* p=s; unsigned v;
    if(radix==10 && value<0){ *p++='-'; v=0u-(unsigned)value; } else v=(unsigned)value;
    char tmp[40]; int n=0;
    if(v==0) tmp[n++]='0';
    else while(v){ unsigned d=v%(unsigned)radix; v/=(unsigned)radix; tmp[n++]=(char)(d<10?'0'+d:'a'+d-10); }
    for(int i=0;i<n;i++) p[i]=tmp[n-1-i];
    p[n]=0; return s;
}
