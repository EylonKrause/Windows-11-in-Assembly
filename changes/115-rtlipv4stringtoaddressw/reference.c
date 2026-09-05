// changes/115-rtlipv4stringtoaddressw/reference.c
// Independent oracle for ntdll!RtlIpv4StringToAddressW — the UTF-16 sibling of 114. Identical
// inet_aton semantics over WCHAR input (a non-ASCII WCHAR is just a non-digit terminator); the
// *Terminator offset is in WCHARs. Validated bit-exact vs the live export before the asm.
typedef unsigned short WCHAR;
#define ERRV 0xC000000DL
long ref_ipv4w(const WCHAR* s, int strict, const WCHAR** term, unsigned char* addr){
    const WCHAR* p=s;
    unsigned long long parts[4]; int n=0;
    for(;;){
        const WCHAR* pstart=p; unsigned long long val=0; int ndig=0; int radix=10;
        if(!strict && p[0]=='0' && (p[1]=='x'||p[1]=='X')){ radix=16; p+=2; }
        else if(!strict && p[0]=='0'){ radix=8; }
        for(;;){
            int c=*p, d;
            if(c>='0'&&c<='9') d=c-'0';
            else if(radix==16 && c>='a'&&c<='f') d=c-'a'+10;
            else if(radix==16 && c>='A'&&c<='F') d=c-'A'+10;
            else break;
            if(d>=radix){ if(radix==8 && ndig==1){ *term=(WCHAR*)p; return ERRV; } break; }
            val=val*radix+d; ndig++;
            if(val>0xFFFFFFFFULL){ *term=(WCHAR*)p; return ERRV; }
            p++;
        }
        if(strict && ndig==1 && pstart[0]=='0' && (*p=='x'||*p=='X')){ *term=(WCHAR*)(p+1); return ERRV; }
        if(ndig==0){ *term=(WCHAR*)((*p=='.'&&n<3)?p+1:p); return ERRV; }
        if(strict && pstart[0]=='0' && ndig>1){ *term=(WCHAR*)(pstart+1); return ERRV; }
        parts[n++]=val;
        if(*p=='.'){ if(n==4){ *term=(WCHAR*)p; return ERRV; } p++; continue; }
        break;
    }
    for(int i=0;i<n-1;i++){ if(parts[i]>255){ *term=(WCHAR*)p; return ERRV; } }
    unsigned long long lastmax = (unsigned long long)((n>=4)?0xFFULL:(n==3)?0xFFFFULL:(n==2)?0xFFFFFFULL:0xFFFFFFFFULL);
    if(parts[n-1]>lastmax){ *term=(WCHAR*)p; return ERRV; }
    if(strict && n!=4){ *term=(WCHAR*)p; return ERRV; }
    unsigned long a=0;
    for(int i=0;i<n-1;i++) a |= (unsigned long)parts[i] << (24-8*i);
    a |= (unsigned long)parts[n-1];
    addr[0]=(a>>24)&0xFF; addr[1]=(a>>16)&0xFF; addr[2]=(a>>8)&0xFF; addr[3]=a&0xFF;
    *term=(WCHAR*)p;
    return 0;
}
