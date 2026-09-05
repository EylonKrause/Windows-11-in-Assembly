// changes/118-rtlguidfromstring/reference.c
// Independent oracle for ntdll!RtlGUIDFromString, validated bit-exact vs the live export before the
// asm. Parses a UNICODE_STRING of the fixed form "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" (exactly 38
// WCHARs; braces at 0/37, dashes at 9/14/19/24; case-insensitive hex) into a GUID. On success:
// Data1 = the 8-hex value, Data2/Data3 = the 4-hex values, Data4[0..7] = the eight 2-hex bytes in
// order. Any format/length/hex error -> STATUS_INVALID_PARAMETER (0xC000000D). No terminator output.
#include <windows.h>
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } WIA_USTR;
#define ERRV 0xC000000DL
static int hx(int c){
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return -1;
}
long ref_guidfromstring(const WIA_USTR* u, GUID* g){
    unsigned chars = u->Length >> 1;
    if(chars < 38) return ERRV;
    if(chars > 38 && u->Buffer[38] != 0) return ERRV;   // char right after '}' must be NUL if present
    const wchar_t* s = u->Buffer;
    if(s[0]!='{'||s[9]!='-'||s[14]!='-'||s[19]!='-'||s[24]!='-'||s[37]!='}') return ERRV;
    unsigned long d1=0; for(int i=1;i<=8;i++){ int v=hx(s[i]); if(v<0)return ERRV; d1=(d1<<4)|v; }
    unsigned d2=0; for(int i=10;i<=13;i++){ int v=hx(s[i]); if(v<0)return ERRV; d2=(d2<<4)|v; }
    unsigned d3=0; for(int i=15;i<=18;i++){ int v=hx(s[i]); if(v<0)return ERRV; d3=(d3<<4)|v; }
    unsigned char d4[8];
    static const int pos[8]={20,22,25,27,29,31,33,35};
    for(int b=0;b<8;b++){ int p=pos[b]; int hi=hx(s[p]),lo=hx(s[p+1]); if(hi<0||lo<0)return ERRV; d4[b]=(unsigned char)((hi<<4)|lo); }
    g->Data1=d1; g->Data2=(unsigned short)d2; g->Data3=(unsigned short)d3;
    for(int b=0;b<8;b++) g->Data4[b]=d4[b];
    return 0;
}
