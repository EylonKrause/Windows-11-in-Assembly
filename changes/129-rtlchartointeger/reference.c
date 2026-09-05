// changes/129-rtlchartointeger/reference.c
// Oracle for ntdll!RtlCharToInteger, validated bit-exact vs the live export over all edges, every byte
// value in leading/embedded position, and 2M fuzz. See RESULTS.md for the reverse-engineered rules.
#define WIA_ERR 0xC000000DL
static int wia_dv(int c){
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='z')return c-'a'+10;
    if(c>='A'&&c<='Z')return c-'A'+10;
    return -1;
}
long ref_char2int(const char* s, unsigned long base, unsigned long* val){
    while(*s && (signed char)*s <= ' ') s++;      // SIGNED compare: skips 0x01-0x20 AND 0x80-0xFF
    int neg=0;
    if(*s=='+') s++; else if(*s=='-'){ neg=1; s++; }
    if(base==0){
        base=10;                                   // bare leading '0' is DECIMAL, not octal
        if(s[0]=='0'){
            if(s[1]=='x'){ base=16; s+=2; }        // lowercase prefixes only
            else if(s[1]=='b'){ base=2; s+=2; }
            else if(s[1]=='o'){ base=8; s+=2; }
        }
    }
    if(base!=2&&base!=8&&base!=10&&base!=16) return WIA_ERR;   // *val untouched
    unsigned long v=0;
    for(;;){ int d=wia_dv((unsigned char)*s); if(d<0||(unsigned long)d>=base) break;
             v=v*base+(unsigned long)d; s++; }     // mod 2^32, no overflow detection
    if(neg) v=(unsigned long)(0u-v);
    *val=v; return 0;
}
