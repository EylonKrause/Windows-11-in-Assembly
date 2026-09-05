// changes/114-rtlipv4stringtoaddress/reference.c
// Independent oracle for ntdll!RtlIpv4StringToAddressA, validated bit-exact vs the live export
// (STATUS + 4-byte address + *Terminator) over 800k fuzz before the asm was written. inet_aton
// semantics: 1-4 '.'-separated parts (a / a.b / a.b.c / a.b.c.d short forms); each part decimal, or
// (non-Strict) octal (leading 0) / hex (0x). Leading parts <=255, the last part fills the remaining
// (5-n) bytes; result is the big-endian assembly. No whitespace/sign skipping. On any malformed input
// -> STATUS_INVALID_PARAMETER (0xC000000D); the *Terminator rules are idiosyncratic (see RESULTS.md).
#define ERRV 0xC000000DL
long ref_ipv4a(const char* s, int strict, const char** term, unsigned char* addr){
    const char* p=s;
    unsigned long long parts[4]; int n=0;
    for(;;){
        const char* pstart=p; unsigned long long val=0; int ndig=0; int radix=10;
        if(!strict && p[0]=='0' && (p[1]=='x'||p[1]=='X')){ radix=16; p+=2; }
        else if(!strict && p[0]=='0'){ radix=8; }
        for(;;){
            int c=*p, d;
            if(c>='0'&&c<='9') d=c-'0';
            else if(radix==16 && c>='a'&&c<='f') d=c-'a'+10;
            else if(radix==16 && c>='A'&&c<='F') d=c-'A'+10;
            else break;
            if(d>=radix){ if(radix==8 && ndig==1){ *term=(char*)p; return ERRV; } break; }
            val=val*radix+d; ndig++;
            if(val>0xFFFFFFFFULL){ *term=(char*)p; return ERRV; }
            p++;
        }
        if(strict && ndig==1 && pstart[0]=='0' && (*p=='x'||*p=='X')){ *term=(char*)(p+1); return ERRV; }
        if(ndig==0){ *term=(char*)((*p=='.'&&n<3)?p+1:p); return ERRV; }
        if(strict && pstart[0]=='0' && ndig>1){ *term=(char*)(pstart+1); return ERRV; }
        parts[n++]=val;
        if(*p=='.'){ if(n==4){ *term=(char*)p; return ERRV; } p++; continue; }
        break;
    }
    for(int i=0;i<n-1;i++){ if(parts[i]>255){ *term=(char*)p; return ERRV; } }
    unsigned long long lastmax = (unsigned long long)((n>=4)?0xFFULL:(n==3)?0xFFFFULL:(n==2)?0xFFFFFFULL:0xFFFFFFFFULL);
    if(parts[n-1]>lastmax){ *term=(char*)p; return ERRV; }
    if(strict && n!=4){ *term=(char*)p; return ERRV; }
    unsigned long a=0;
    for(int i=0;i<n-1;i++) a |= (unsigned long)parts[i] << (24-8*i);
    a |= (unsigned long)parts[n-1];
    addr[0]=(a>>24)&0xFF; addr[1]=(a>>16)&0xFF; addr[2]=(a>>8)&0xFF; addr[3]=a&0xFF;
    *term=(char*)p;
    return 0;
}
