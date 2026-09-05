// changes/116-rtlipv4stringtoaddressex/reference.c
// Independent oracle for ntdll!RtlIpv4StringToAddressExA, validated bit-exact vs the live export
// (STATUS + address + network-order port) over 1M fuzz before the asm. The address is the same
// inet_aton parser as 114 (committed to *Addr as soon as it parses); then an optional ":port"
// (same number parser, but a lone octal "0" is rejected, the value must fit a USHORT, and the whole
// string must be consumed) is stored to *Port in network byte order. There is no Terminator output.
#include <string.h>
#define ERRV 0xC000000DL
static long parse_addr(const char* s, int strict, const char** term, unsigned char* addr){
    const char* p=s; unsigned long long parts[4]; int n=0;
    for(;;){
        const char* pstart=p; unsigned long long val=0; int ndig=0; int radix=10;
        if(!strict && p[0]=='0' && (p[1]=='x'||p[1]=='X')){ radix=16; p+=2; }
        else if(!strict && p[0]=='0'){ radix=8; }
        for(;;){ int c=*p,d;
            if(c>='0'&&c<='9')d=c-'0'; else if(radix==16&&c>='a'&&c<='f')d=c-'a'+10;
            else if(radix==16&&c>='A'&&c<='F')d=c-'A'+10; else break;
            if(d>=radix){ if(radix==8&&ndig==1){*term=(char*)p;return ERRV;} break; }
            val=val*radix+d; ndig++; if(val>0xFFFFFFFFULL){*term=(char*)p;return ERRV;} p++;
        }
        if(strict&&ndig==1&&pstart[0]=='0'&&(*p=='x'||*p=='X')){*term=(char*)(p+1);return ERRV;}
        if(ndig==0){*term=(char*)((*p=='.'&&n<3)?p+1:p);return ERRV;}
        if(strict&&pstart[0]=='0'&&ndig>1){*term=(char*)(pstart+1);return ERRV;}
        parts[n++]=val;
        if(*p=='.'){ if(n==4){*term=(char*)p;return ERRV;} p++; continue; }
        break;
    }
    for(int i=0;i<n-1;i++) if(parts[i]>255){*term=(char*)p;return ERRV;}
    unsigned long long lm=(n>=4)?0xFFULL:(n==3)?0xFFFFULL:(n==2)?0xFFFFFFULL:0xFFFFFFFFULL;
    if(parts[n-1]>lm){*term=(char*)p;return ERRV;}
    if(strict&&n!=4){*term=(char*)p;return ERRV;}
    unsigned long a=0; for(int i=0;i<n-1;i++)a|=(unsigned long)parts[i]<<(24-8*i); a|=(unsigned long)parts[n-1];
    addr[0]=(a>>24)&0xFF;addr[1]=(a>>16)&0xFF;addr[2]=(a>>8)&0xFF;addr[3]=a&0xFF; *term=(char*)p; return 0;
}
long ref_ipv4exa(const char* s, int strict, unsigned char* addr, unsigned short* port){
    const char* term; unsigned char a[4];
    long st=parse_addr(s,strict,&term,a);
    if(st!=0) return st;
    memcpy(addr,a,4);
    if(*term==0){ *port=0; return 0; }
    if(*term!=':') return ERRV;
    const char* p=term+1;
    unsigned long long val=0; int ndig=0, radix=10;
    if(p[0]=='0'&&(p[1]=='x'||p[1]=='X')){ radix=16; p+=2; } else if(p[0]=='0'){ radix=8; }
    for(;;){ int c=*p,d;
        if(c>='0'&&c<='9')d=c-'0'; else if(radix==16&&c>='a'&&c<='f')d=c-'a'+10;
        else if(radix==16&&c>='A'&&c<='F')d=c-'A'+10; else break;
        if(d>=radix){ if(radix==8&&ndig==1) return ERRV; break; }
        val=val*radix+d; ndig++; if(val>0xFFFFFFFFULL) return ERRV; p++;
    }
    if(ndig==0) return ERRV;
    if(radix==8&&ndig==1) return ERRV;
    if(*p!=0) return ERRV;
    if(val>0xFFFF) return ERRV;
    unsigned short v=(unsigned short)val;
    *port=(unsigned short)((v>>8)|(v<<8));
    return 0;
}
