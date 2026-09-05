// changes/112-strtoi64/reference.c
// Independent oracle for ucrtbase!_strtoi64, validated bit-exact vs the live export (value+endptr+
// errno) over 800k fuzz before the asm was written. Same parse as strtol; 64-bit signed with a
// cutoff/cutlim overflow test saturating to _I64_MAX / _I64_MIN and errno=ERANGE. (The asm uses an
// equivalent mul-based overflow guard; both saturate at the same magnitude.)
#include <errno.h>
static const unsigned char* pfx(const unsigned char* s,int* neg,int* base){
    while(*s==0x20||(*s>=0x09&&*s<=0x0D))s++;
    *neg=0; if(*s=='-'){*neg=1;s++;} else if(*s=='+')s++;
    if(*base==0)*base=(*s=='0')?((s[1]=='x'||s[1]=='X')?16:8):10;
    if(*base==16&&s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2;
    return s;
}
static int dv(int c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='z')return c-'a'+10; if(c>='A'&&c<='Z')return c-'A'+10; return 99; }
long long ref_strtoi64(const char* nptr, char** endptr, int base){
    int neg; const unsigned char* s=pfx((const unsigned char*)nptr,&neg,&base);
    const unsigned char* dg=s;
    unsigned long long limit = neg?0x8000000000000000ULL:0x7FFFFFFFFFFFFFFFULL;
    unsigned long long cutoff=limit/base; int cutlim=(int)(limit%base);
    unsigned long long acc=0; int ovf=0;
    for(;;){ int d=dv(*s); if(d>=base)break;
        if(!ovf){ if(acc>cutoff||(acc==cutoff&&d>cutlim))ovf=1; else acc=acc*base+d; } s++; }
    if(s==dg){ if(endptr)*endptr=(char*)nptr; return 0; }
    if(endptr)*endptr=(char*)s;
    if(ovf){ errno=ERANGE; return neg?(long long)0x8000000000000000ULL:(long long)0x7FFFFFFFFFFFFFFFULL; }
    return neg?(long long)(0ULL-acc):(long long)acc;
}
