// changes/108-atoi/reference.c
// Independent scalar oracle for ucrtbase!atoi: skip C-locale whitespace {09 0A 0B 0C 0D 20},
// one optional +/- sign, decimal digits until a non-digit; overflow saturates to INT_MAX/INT_MIN.
int ref_atoi(const char* s){
    const unsigned char* p=(const unsigned char*)s;
    while(*p==0x20 || (*p>=0x09 && *p<=0x0D)) p++;
    int neg=0;
    if(*p=='-'){ neg=1; p++; } else if(*p=='+'){ p++; }
    unsigned long long acc=0;
    while(*p>='0' && *p<='9'){
        acc = acc*10 + (unsigned)(*p - '0');
        if(acc > 0x100000000ULL) acc = 0x100000000ULL;   // cap; still > any 32-bit magnitude
        p++;
    }
    if(!neg) return acc > 0x7FFFFFFFULL ? 0x7FFFFFFF : (int)acc;
    return acc > 0x80000000ULL ? (int)0x80000000 : -(int)acc;
}
