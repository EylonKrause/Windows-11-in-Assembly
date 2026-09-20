/* Derive iphlpapi!ConvertGuidToStringW and ConvertGuidToStringA.
 *
 * WHY: at 315 ns (W) and 275 ns (A) per call these are, after RtlIsTextUnicode, the most
 * expensive thing found in the whole System32 headroom survey, and for a reason that is almost
 * comic. The function does not format the GUID at all. The disassembly spills the eleven GUID
 * fields to the stack as varargs, loads the literal format string
 *     "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}"
 * and calls a StringCchPrintfW clone that re-parses that format on every single call and
 * dispatches each conversion through a per-character output helper. Writing 38 characters from a
 * nibble table should cost well under 10 ns.
 *
 * Unknowns to pin (the survey's vet agent proposed these; every one is re-measured here, because
 * a vet agent's summary is not a contract):
 *   1. exact output: case, braces, field order and widths.
 *   2. NULL Guid / NULL String.
 *   3. StringLenInChars == 0.
 *   4. 1 <= cch <= 38: is the buffer left untouched, or truncated-and-terminated?
 *   5. cch >= 39.
 *   6. cch >= 0x80000000, claimed to return 122, not 87, with String[0] = 0.
 *   7. is the A form byte-for-byte the same string?
 *   8. unaligned GUID pointer.
 * Build: cl /nologo /O2 /MD cgs.c && cgs.exe
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef DWORD (WINAPI *CGSW)(const GUID*, PWSTR, DWORD);
typedef DWORD (WINAPI *CGSA)(const GUID*, PSTR,  DWORD);
static CGSW gw; static CGSA ga;

#define PW 0x2A2A
#define PB '\x7F'

static void showW(const GUID* g, DWORD cch, const char* what){
    static wchar_t b[128];
    for(int i=0;i<128;i++) b[i]=PW;
    DWORD r = gw(g,b,cch);
    printf("  %-34s cch=%-12u -> %-4u [", what, cch, r);
    for(int i=0;i<44;i++){
        if(b[i]==PW) { printf("."); }
        else if(b[i]==0) printf("0");
        else if(b[i]<128) printf("%c",(char)b[i]);
        else printf("?");
    }
    printf("]\n");
}
static void showA(const GUID* g, DWORD cch, const char* what){
    static char b[128];
    for(int i=0;i<128;i++) b[i]=PB;
    DWORD r = ga(g,b,cch);
    printf("  %-34s cch=%-12u -> %-4u [", what, cch, r);
    for(int i=0;i<44;i++){
        if(b[i]==PB) printf(".");
        else if(b[i]==0) printf("0");
        else printf("%c", b[i]);
    }
    printf("]\n");
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE h = LoadLibraryW(L"iphlpapi.dll");
    gw = (CGSW)GetProcAddress(h,"ConvertGuidToStringW");
    ga = (CGSA)GetProcAddress(h,"ConvertGuidToStringA");
    if(!gw||!ga){ printf("missing export\n"); return 1; }
    printf("ConvertGuidToStringW @%p  ConvertGuidToStringA @%p\n\n",(void*)gw,(void*)ga);

    GUID g = {0xDEADBEEF,0x1234,0x5678,{0x9A,0xBC,0xDE,0xF0,0x11,0x22,0x33,0x44}};
    GUID z = {0,0,0,{0,0,0,0,0,0,0,0}};
    GUID f = {0xFFFFFFFF,0xFFFF,0xFFFF,{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}};

    printf("== 1. the output itself ==\n");
    showW(&g,64,"DEADBEEF...");
    showW(&z,64,"all zero");
    showW(&f,64,"all FF");
    showA(&g,64,"DEADBEEF... (A)");

    printf("\n== 2/3. NULL pointers and cch 0 ==\n");
    { static wchar_t b[64]; for(int i=0;i<64;i++) b[i]=PW;
      printf("  NULL Guid                 -> %u  (buf[0]=%04X)\n", gw(NULL,b,64), b[0]); }
    printf("  NULL String               -> %u\n", gw(&g,NULL,64));
    showW(&g,0,"cch 0");

    printf("\n== 4. every cch from 1 to 42: truncation or nothing? ==\n");
    for(DWORD c=1;c<=42;c++) showW(&g,c,"cch sweep");

    printf("\n== 6. absurd cch ==\n");
    showW(&g,0x7FFFFFFFu,"cch 0x7FFFFFFF");
    showW(&g,0x80000000u,"cch 0x80000000");
    showW(&g,0xFFFFFFFFu,"cch 0xFFFFFFFF");

    printf("\n== 7. does the A form agree, character for character? ==\n");
    {
        unsigned long sd=991; int bad=0;
        static wchar_t bw[128]; static char ba[128];
        for(int t=0;t<200000;t++){
            GUID q; unsigned char* p=(unsigned char*)&q;
            for(int i=0;i<16;i++){ sd=sd*1103515245u+12345u; p[i]=(unsigned char)(sd>>16); }
            sd=sd*1103515245u+12345u; DWORD cch = sd%50;
            for(int i=0;i<128;i++){ bw[i]=PW; ba[i]=PB; }
            DWORD rw = gw(&q,bw,cch);
            DWORD ra = ga(&q,ba,cch);
            if(rw!=ra){ if(bad<5) printf("   RET DIFF cch=%u w=%u a=%u\n",cch,rw,ra); ++bad; continue; }
            for(int i=0;i<50;i++){
                int uw = (bw[i]==PW), ua = (ba[i]==(char)PB);
                if(uw!=ua || (!uw && (unsigned)bw[i]!=(unsigned char)ba[i])){
                    if(bad<5) printf("   CHAR DIFF at %d cch=%u %04X vs %02X\n",i,cch,bw[i],(unsigned char)ba[i]);
                    ++bad; break;
                }
            }
        }
        printf("  %d differences of 200000  %s\n", bad, bad?"":"IDENTICAL SHAPE");
    }

    printf("\n== 8. unaligned GUID pointer ==\n");
    {
        static unsigned char raw[64];
        memcpy(raw+3,&g,16);
        showW((const GUID*)(raw+3),64,"GUID at +3");
    }
    return 0;
}
