// changes/110-strtol/reference.c
// Independent oracle for ucrtbase!strtol, validated bit-exact vs the live export (value + endptr +
// errno) across all edge cases and 600k fuzz strings before the asm was written. Contract: skip
// C-locale whitespace, one optional sign, base 0 (0x->16, leading 0->8, else 10) / 2..36; consume a
// "0x"/"0X" prefix for base 16 and treat a prefix with no hex digit as no-conversion (endptr=nptr);
// endptr = first unparsed char; overflow saturates to LONG_MAX/LONG_MIN with errno=ERANGE.
#include <errno.h>
long ref_strtol(const char* nptr, char** endptr, int base){
    const unsigned char* s=(const unsigned char*)nptr;
    while(*s==0x20 || (*s>=0x09 && *s<=0x0D)) s++;
    int neg=0;
    if(*s=='-'){ neg=1; s++; } else if(*s=='+'){ s++; }
    if(base==0){ base=(*s=='0') ? ((s[1]=='x'||s[1]=='X')?16:8) : 10; }
    if(base==16 && s[0]=='0' && (s[1]=='x'||s[1]=='X')) s+=2;
    const unsigned char* digstart=s;
    unsigned long long acc=0; int ovf=0;
    for(;;){
        int c=*s,d;
        if(c>='0'&&c<='9')d=c-'0';
        else if(c>='a'&&c<='z')d=c-'a'+10;
        else if(c>='A'&&c<='Z')d=c-'A'+10;
        else break;
        if(d>=base) break;
        if(!ovf){ acc=acc*base+d; if(acc>0x100000000ULL){ acc=0x100000000ULL; ovf=1; } }
        s++;
    }
    if(s==digstart){ if(endptr)*endptr=(char*)nptr; return 0; }
    if(endptr)*endptr=(char*)s;
    unsigned long long limit = neg?2147483648ULL:2147483647ULL;
    if(acc>limit){ errno=ERANGE; return neg?(long)0x80000000:(long)0x7FFFFFFF; }
    return neg?-(long)acc:(long)acc;
}
