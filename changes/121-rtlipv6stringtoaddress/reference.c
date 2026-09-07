// changes/121-rtlipv6stringtoaddress/reference.c
// Independent oracle for ntdll!RtlIpv6StringToAddressA, validated bit-exact vs the live export
// (STATUS + 16 address bytes + *Terminator) over 5M fuzz before the asm. BSD-inet_pton6 core adapted
// to Windows's lenient stop + terminator rules and embedded IPv4. See RESULTS.md for the rule catalog.
#include <string.h>
#define ERRV 0xC000000DL
static int hx(int c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
/* The live export does not store the value its structural scan accumulated: it re-reads the group
   from the token start with a number helper that honours a "0x"/"0X" prefix and accumulates in 32
   bits, saturating to 0xFFFF the moment a shift would overflow.  That is why "::0x9" is 9 even
   though the scan stops dead at the 'x', and why the scan's `seen` (which drives the >4-digit
   error) counts something different from what the value sees. */
static unsigned grpval(const char* q){
    unsigned val=0;
    if(q[0]=='0' && (q[1]=='x'||q[1]=='X')) q+=2;
    for(;;){ int v=hx((unsigned char)*q); if(v<0) return val;
             if(val & 0xF8000000u) return 0xFFFFu;   // signed-32-bit overflow -> saturate
             val=(val<<4)|(unsigned)v; q++; }
}
long ref_ip6(const char* s, const char** term, unsigned char* addr){
    unsigned char tmp[16]; memset(tmp,0,16);
    unsigned char* tp=tmp; unsigned char* endp=tmp+16; unsigned char* colonp=0;
    const char* p=s; int seen=0; unsigned val=0; const char* curtok;
    if(*p==':'){ p++; if(*p!=':'){ *term=(char*)s; return ERRV; } }
    curtok=p;
    for(;;){
        int c=(unsigned char)*p; int v=hx(c);
        if(v>=0){ val=(val<<4)|v; seen++; p++; continue; }
        if(c=='.' && seen){
            int alldec=1; for(const char* qq=curtok;qq<p;qq++) if(*qq<'0'||*qq>'9'){ alldec=0; break; }
            if(alldec && tp+4 <= endp-(colonp?2:0)){
                const char* q=curtok;
                for(int oc=0; oc<4; oc++){
                    if(*q<'0'||*q>'9'){ *term=(char*)q; return ERRV; }
                    int ov=0, nd=0; while(*q>='0'&&*q<='9'){ ov=ov*10+(*q-'0'); if(ov>65535)ov=65535; q++; nd++; }
                    if(oc<3 && *q!='.'){ *term=(char*)q; return ERRV; }   // separator is checked BEFORE the octet is validated
                    if(nd>3 || ov>255){ if(oc<3) return ERRV; *term=(char*)q; return ERRV; }
                    *tp++=(unsigned char)ov;
                    if(oc<3) q++;
                }
                p=q; seen=0; break;
            }
            if(seen>4){ *term=(char*)p; return ERRV; }
            break;
        }
        if(c==':'){
            if(!seen){
                if(colonp){ *term=(char*)p; break; }
                colonp=tp; p++; curtok=p; continue;
            }
            if(seen>4){ if(colonp && p[1]==':') *term=(char*)p; return ERRV; }
            *tp++=(unsigned char)(val>>8); *tp++=(unsigned char)val; seen=0; val=0;
            if(tp==endp || (colonp && tp+2==endp)){ *term=(char*)p; break; }
            if(p[1]==':'){
                if(colonp){ *term=(char*)p; break; }
                colonp=tp; p+=2; curtok=p;
                if(tp+2==endp){ *term=(char*)p; break; }
                continue;
            }
            p++; curtok=p;
            if(hx((unsigned char)*p)<0 && *p!=':'){ *term=(char*)p; return ERRV; }
            continue;
        }
        break;
    }
    if(seen){
        if(seen>4){ *term=(char*)p; return ERRV; }   // final group: the live export DOES set it here
        if(tp+2>endp){ *term=(char*)p; return ERRV; }
        val=grpval(curtok);
        *tp++=(unsigned char)(val>>8); *tp++=(unsigned char)val;
    }
    if(colonp){
        int n=(int)(tp-colonp);
        for(int i=1;i<=n;i++){ endp[-i]=colonp[n-i]; colonp[n-i]=0; }
        tp=endp;
    }
    if(tp!=endp){ *term=(char*)p; return ERRV; }
    memcpy(addr,tmp,16); *term=(char*)p; return 0;
}
