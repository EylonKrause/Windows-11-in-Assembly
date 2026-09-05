// changes/122-rtlipv6stringtoaddressex/reference.c
// Independent oracle for ntdll!RtlIpv6StringToAddressExA, validated bit-exact vs the live export
// (STATUS + address + scope-id + network-order port) over 1.5M fuzz before the asm. Wraps the IPv6
// address core (121) with optional '[' ']' brackets, a '%<decimal>' scope-id, and a bracketed
// ':<port>'; the whole string must be consumed. No Terminator output. See RESULTS.md for the rules.
#include <string.h>
#define ERRV 0xC000000DL
static int hx(int c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
static long ref_ip6(const char* s, const char** term, unsigned char* addr){
    unsigned char tmp[16]; memset(tmp,0,16);
    unsigned char* tp=tmp; unsigned char* endp=tmp+16; unsigned char* colonp=0;
    const char* p=s; int seen=0; unsigned val=0; const char* curtok;
    if(*p==':'){ p++; if(*p!=':'){ *term=(char*)s; return ERRV; } }
    curtok=p;
    for(;;){ int c=(unsigned char)*p; int v=hx(c);
        if(v>=0){ val=(val<<4)|v; seen++; p++; continue; }
        if(c=='.' && seen){
            int alldec=1; for(const char* qq=curtok;qq<p;qq++) if(*qq<'0'||*qq>'9'){ alldec=0; break; }
            if(alldec && tp+4 <= endp-(colonp?2:0)){
                const char* q=curtok;
                for(int oc=0; oc<4; oc++){
                    if(*q<'0'||*q>'9'){ *term=(char*)q; return ERRV; }
                    int ov=0, nd=0; while(*q>='0'&&*q<='9'){ ov=ov*10+(*q-'0'); if(ov>65535)ov=65535; q++; nd++; }
                    if(nd>3 || ov>255){ if(oc<3) return ERRV; *term=(char*)q; return ERRV; }
                    *tp++=(unsigned char)ov;
                    if(oc<3){ if(*q!='.'){ *term=(char*)q; return ERRV; } q++; } }
                p=q; seen=0; break; }
            if(seen>4){ *term=(char*)p; return ERRV; } break; }
        if(c==':'){
            if(!seen){ if(colonp){ *term=(char*)p; break; } colonp=tp; p++; curtok=p; continue; }
            if(seen>4){ return ERRV; }
            *tp++=(unsigned char)(val>>8); *tp++=(unsigned char)val; seen=0; val=0;
            if(tp==endp || (colonp && tp+2==endp)){ *term=(char*)p; break; }
            if(p[1]==':'){ if(colonp){ *term=(char*)p; break; } colonp=tp; p+=2; curtok=p; if(tp+2==endp){ *term=(char*)p; break; } continue; }
            p++; curtok=p; if(hx((unsigned char)*p)<0 && *p!=':'){ *term=(char*)p; return ERRV; } continue; }
        break; }
    if(seen){ if(seen>4){ return ERRV; } if(tp+2>endp){ *term=(char*)p; return ERRV; } *tp++=(unsigned char)(val>>8); *tp++=(unsigned char)val; }
    if(colonp){ int n=(int)(tp-colonp); for(int i=1;i<=n;i++){ endp[-i]=colonp[n-i]; colonp[n-i]=0; } tp=endp; }
    if(tp!=endp){ *term=(char*)p; return ERRV; }
    memcpy(addr,tmp,16); *term=(char*)p; return 0;
}
static const char* pport(const char* p, unsigned* out){
    int radix=10;
    if(p[0]=='0' && (p[1]=='x'||p[1]=='X')){ radix=16; p+=2; } else if(p[0]=='0'){ radix=8; }
    unsigned long long v=0; int nd=0;
    for(;;){ int c=*p,d;
        if(c>='0'&&c<='9')d=c-'0'; else if(radix==16&&c>='a'&&c<='f')d=c-'a'+10; else if(radix==16&&c>='A'&&c<='F')d=c-'A'+10; else break;
        if(d>=radix){ if(radix==8&&nd==1) return 0; break; }
        v=v*radix+d; nd++; if(v>0xFFFF) return 0; p++; }
    if(nd==0){ if(radix==16){ *out=0; return p; } return 0; }
    if(v>0xFFFF) return 0;
    *out=(unsigned)v; return p;
}
long ref_ip6exa(const char* s, unsigned char* addr, unsigned long* scope, unsigned short* port){
    const char* p=s; int bracket=0;
    if(*p=='['){ bracket=1; p++; }
    const char* aterm; unsigned char a[16];
    long st=ref_ip6(p,&aterm,a);
    if(st!=0) return st;
    const char* q=aterm; unsigned long sc=0;
    if(*q=='%'){
        q++;
        if(*q<'0'||*q>'9') return ERRV;
        unsigned long long v=0; while(*q>='0'&&*q<='9'){ v=v*10+(*q-'0'); if(v>0xFFFFFFFF) return ERRV; q++; }
        sc=(unsigned long)v;
    }
    unsigned pt=0;
    if(bracket){
        if(*q!=']') return ERRV;
        q++;
        if(*q==':'){ q++; if(*q==0){ pt=0; } else { const char* e=pport(q,&pt); if(!e) return ERRV; q=e; } }
    }
    if(*q!=0) return ERRV;
    memcpy(addr,a,16); *scope=sc; { unsigned short v=(unsigned short)pt; *port=(unsigned short)((v>>8)|(v<<8)); }
    return 0;
}
