// changes/109-atoi64/reference.c
// Independent oracle for ucrtbase!_atoi64: skip C-locale whitespace {09 0A 0B 0C 0D 20}, one
// optional +/- sign, decimal digits until a non-digit; 64-bit magnitude saturating to
// _I64_MAX / _I64_MIN on overflow.
long long ref_atoi64(const char* s){
    const unsigned char* p=(const unsigned char*)s;
    while(*p==0x20 || (*p>=0x09 && *p<=0x0D)) p++;
    int neg=0;
    if(*p=='-'){ neg=1; p++; } else if(*p=='+'){ p++; }
    unsigned long long acc=0;
    const unsigned long long CAP=0x8000000000000000ULL, DIV=0x1999999999999999ULL;
    while(*p>='0' && *p<='9'){
        if(acc>=DIV){ acc=CAP; break; }
        acc = acc*10 + (unsigned)(*p - '0');
        if(acc>=CAP){ acc=CAP; break; }
        p++;
    }
    if(!neg) return acc>=CAP ? (long long)0x7FFFFFFFFFFFFFFFULL : (long long)acc;
    return acc>=CAP ? (long long)0x8000000000000000ULL : -(long long)acc;
}
