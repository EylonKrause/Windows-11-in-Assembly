// changes/166-rtlipv6stringtoaddressw/reference.c
// Independent oracle for ntdll!RtlIpv6StringToAddressW. Same rule catalog as change 121's ANSI
// oracle (BSD-inet_pton6 core adapted to Windows's lenient stop + terminator rules and embedded
// IPv4), lifted to UTF-16, plus the one rule the wide export has and the ANSI one cannot:
//
//   * the STRUCTURE scan is ASCII-only and compares the FULL 16-bit unit, so a unit above 255 is
//     "not a hex digit, not '.', not ':'" and stops the scan -- there is no low-byte aliasing
//     (U+0141 is not 'A', U+013A is not ':');
//   * but the VALUE is a re-parse of the token from its start by a number helper that runs past
//     where the scan stopped: it honours a "0x"/"0X" prefix, accepts ASCII hex plus the 17 Unicode
//     decimal-digit blocks below, and never moves the parse pointer, `seen`/`nd` or *Terminator.
//
// Both rules were derived by sweeping all 65536 units against the live export, not assumed.
#include <string.h>
#include <wchar.h>
#define ERRV 0xC000000DL
static int hx(unsigned c){ if(c>='0'&&c<='9')return (int)c-'0'; if(c>='a'&&c<='f')return (int)c-'a'+10; if(c>='A'&&c<='F')return (int)c-'A'+10; return -1; }
static const unsigned short UDIG[17]={0x0660,0x06F0,0x0966,0x09E6,0x0A66,0x0AE6,0x0B66,0x0C66,0x0CE6,
                                      0x0D66,0x0E50,0x0ED0,0x0F20,0x1040,0x17E0,0x1810,0xFF10};
static int udig(unsigned c){ for(int i=0;i<17;i++){ unsigned d=c-UDIG[i]; if(d<10) return (int)d; } return -1; }
// The group value is a RE-PARSE of the token from its start, not the scan's accumulator: a "0x"/
// "0X" prefix is honoured and the accumulator is 32-bit, saturating to 0xFFFF the moment a shift
// would overflow.  Identical to the ANSI helper in change 121 except for the Unicode digits.
static unsigned grpval(const wchar_t* q){
    unsigned val=0;
    if(q[0]==L'0' && (q[1]==L'x'||q[1]==L'X')) q+=2;
    for(;;){ unsigned c=(unsigned)*q; int v=hx(c); if(v<0) v=udig(c); if(v<0) return val;
             if(val & 0xF8000000u) return 0xFFFFu;   // signed-32-bit overflow -> saturate
             val=(val<<4)|(unsigned)v; q++; }
}
// embedded-IPv4 octet: base 10, keeps ntdll's 65535 cap
static int occont(const wchar_t* q, int ov){
    for(;;){ unsigned c=(unsigned)*q; int v=(c>=L'0'&&c<=L'9')?(int)c-L'0':udig(c); if(v<0) return ov;
             ov=ov*10+v; if(ov>65535) ov=65535; q++; }
}
long ref_ip6w(const wchar_t* s, const wchar_t** term, unsigned char* addr){
    unsigned char tmp[16]; memset(tmp,0,16);
    unsigned char* tp=tmp; unsigned char* endp=tmp+16; unsigned char* colonp=0;
    const wchar_t* p=s; int seen=0; unsigned val=0; const wchar_t* curtok;
    if(*p==L':'){ p++; if(*p!=L':'){ *term=s; return ERRV; } }
    curtok=p;
    for(;;){
        unsigned c=(unsigned)*p; int v=hx(c);
        if(v>=0){ val=(val<<4)|v; seen++; p++; continue; }
        if(c==L'.' && seen){
            int alldec=1; for(const wchar_t* qq=curtok;qq<p;qq++) if(*qq<L'0'||*qq>L'9'){ alldec=0; break; }
            if(alldec && tp+4 <= endp-(colonp?2:0)){
                const wchar_t* q=curtok;
                for(int oc=0; oc<4; oc++){
                    if(*q<L'0'||*q>L'9'){ *term=q; return ERRV; }
                    int ov=0, nd=0; while(*q>=L'0'&&*q<=L'9'){ ov=ov*10+(*q-L'0'); if(ov>65535)ov=65535; q++; nd++; }
                    ov=occont(q,ov);          // extended digits: value only, q and nd unmoved
                    if(oc<3 && *q!=L'.'){ *term=q; return ERRV; }   // separator checked BEFORE validation
                    if(nd>3 || ov>255){ if(oc<3) return ERRV; *term=q; return ERRV; }
                    *tp++=(unsigned char)ov;
                    if(oc<3) q++;
                }
                p=q; seen=0; break;
            }
            if(seen>4){ *term=p; return ERRV; }
            break;
        }
        if(c==L':'){
            if(!seen){
                if(colonp){ *term=p; break; }
                colonp=tp; p++; curtok=p; continue;
            }
            if(seen>4){ if(colonp && p[1]==L':') *term=p; return ERRV; }
            *tp++=(unsigned char)(val>>8); *tp++=(unsigned char)val; seen=0; val=0;
            if(tp==endp || (colonp && tp+2==endp)){ *term=p; break; }
            if(p[1]==L':'){
                if(colonp){ *term=p; break; }
                colonp=tp; p+=2; curtok=p;
                if(tp+2==endp){ *term=p; break; }
                continue;
            }
            p++; curtok=p;
            if(hx((unsigned)*p)<0 && *p!=L':'){ *term=p; return ERRV; }
            continue;
        }
        break;
    }
    if(seen){
        if(seen>4){ *term=p; return ERRV; }   // the live export DOES set it for a long final group
        if(tp+2>endp){ *term=p; return ERRV; }
        val=grpval(curtok);
        *tp++=(unsigned char)(val>>8); *tp++=(unsigned char)val;
    }
    if(colonp){
        int n=(int)(tp-colonp);
        for(int i=1;i<=n;i++){ endp[-i]=colonp[n-i]; colonp[n-i]=0; }
        tp=endp;
    }
    if(tp!=endp){ *term=p; return ERRV; }
    memcpy(addr,tmp,16); *term=p; return 0;
}
