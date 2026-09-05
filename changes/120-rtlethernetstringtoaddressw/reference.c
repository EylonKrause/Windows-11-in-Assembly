// changes/120-rtlethernetstringtoaddressw/reference.c
// Independent oracle for ntdll!RtlEthernetStringToAddressW — the UTF-16 sibling of 119. Identical
// MAC parse over WCHAR input (a WCHAR >= 0x100 is neither hex nor a separator); *Terminator offset in
// WCHARs. Validated bit-exact vs the live export before the asm.
typedef unsigned short WCHAR;
#define ERRV 0xC000000DL
static int hx(int c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
long ref_ethstrw(const WCHAR* s, const WCHAR** term, unsigned char* addr){
    const WCHAR* p=s;
    for(int g=0; g<6; g++){
        int hi=hx(*p);
        if(hi<0){ if(*p=='-'||*p==':') p++; *term=(WCHAR*)p; return ERRV; }
        p++;
        int lo=hx(*p);
        if(lo<0){ if(*p=='-'||*p==':') p++; *term=(WCHAR*)p; return ERRV; }
        p++;
        addr[g]=(unsigned char)((hi<<4)|lo);
        if(g<5){
            if(*p=='-'||*p==':') p++;
            else { *term=(WCHAR*)p; return ERRV; }
        }
    }
    { int c=*p; if(hx(c)>=0 || c=='-' || c==':'){ *term=(WCHAR*)p; return ERRV; } }
    *term=(WCHAR*)p;
    return 0;
}
