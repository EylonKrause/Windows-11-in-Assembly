// changes/119-rtlethernetstringtoaddress/reference.c
// Independent oracle for ntdll!RtlEthernetStringToAddressA, validated bit-exact vs the live export
// (STATUS + 6 address bytes + *Terminator) over 1.5M fuzz before the asm. Format: six groups of
// exactly two hex digits separated by '-' or ':' (the two separators may be mixed). *Terminator = the
// first char after the 6th group (a hex digit there is an error). Malformed -> STATUS_INVALID_PARAMETER
// (0xC000000D). Terminator quirk: when a hex digit is expected but a separator appears, the separator
// is consumed before the error is reported (so the terminator points past it).
#define ERRV 0xC000000DL
static int hx(int c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
long ref_ethstr(const char* s, const char** term, unsigned char* addr){
    const char* p=s;
    for(int g=0; g<6; g++){
        int hi=hx((unsigned char)*p);
        if(hi<0){ if(*p=='-'||*p==':') p++; *term=(char*)p; return ERRV; }
        p++;
        int lo=hx((unsigned char)*p);
        if(lo<0){ if(*p=='-'||*p==':') p++; *term=(char*)p; return ERRV; }
        p++;
        addr[g]=(unsigned char)((hi<<4)|lo);
        if(g<5){
            if(*p=='-'||*p==':') p++;
            else { *term=(char*)p; return ERRV; }
        }
    }
    { int c=(unsigned char)*p; if(hx(c)>=0 || c=='-' || c==':'){ *term=(char*)p; return ERRV; } }
    *term=(char*)p;
    return 0;
}
