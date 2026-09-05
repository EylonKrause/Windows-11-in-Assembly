// changes/111-strtoul/reference.c
// Independent oracle for ucrtbase!strtoul: identical parse to strtol, unsigned result in
// [0, ULONG_MAX]; magnitude > 0xFFFFFFFF saturates to ULONG_MAX with errno=ERANGE; a '-' sign
// negates the (non-overflowed) magnitude modulo 2^32.
#include <errno.h>
unsigned long ref_strtoul(const char* nptr, char** endptr, int base){
    const unsigned char* s=(const unsigned char*)nptr;
    while(*s==0x20 || (*s>=0x09 && *s<=0x0D)) s++;
    int neg=0;
    if(*s=='-'){ neg=1; s++; } else if(*s=='+'){ s++; }
    if(base==0){ base=(*s=='0') ? ((s[1]=='x'||s[1]=='X')?16:8) : 10; }
    if(base==16 && s[0]=='0' && (s[1]=='x'||s[1]=='X')) s+=2;
    const unsigned char* digstart=s;
    unsigned long long acc=0;
    for(;;){
        int c=*s,d;
        if(c>='0'&&c<='9')d=c-'0';
        else if(c>='a'&&c<='z')d=c-'a'+10;
        else if(c>='A'&&c<='Z')d=c-'A'+10;
        else break;
        if(d>=base) break;
        if(acc<=0xFFFFFFFFULL){ acc=acc*base+d; if(acc>0x100000000ULL) acc=0x100000000ULL; }
        s++;
    }
    if(s==digstart){ if(endptr)*endptr=(char*)nptr; return 0; }
    if(endptr)*endptr=(char*)s;
    if(acc>0xFFFFFFFFULL){ errno=ERANGE; return 0xFFFFFFFFUL; }
    unsigned long r=(unsigned long)acc;
    return neg ? (unsigned long)(0u - r) : r;
}
