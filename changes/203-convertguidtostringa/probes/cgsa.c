/* changes/203-convertguidtostringa/probes/cgsa.c
   Pin down iphlpapi!ConvertGuidToStringA against the live export before writing any assembly.

   Change 202 established the WIDE contract. The narrow one is NOT assumed to match: it is a
   separate export, and this project has repeatedly found the A/W pair of a routine carrying
   different conventions. Everything below is measured:
     * the four length regimes (full / truncating / zero / absurd) and their exact buffer effects;
     * whether the absurd-length threshold is the same 0x80000000;
     * whether the characters are identical to the wide form's, cell for cell;
     * the cost, so the target is worth the work. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef DWORD (WINAPI *FNA)(const GUID*, PSTR,  DWORD);
typedef DWORD (WINAPI *FNW)(const GUID*, PWSTR, DWORD);

#define PB '\x2A'
#define DSZ 160

static FNA A;
static FNW W;

static void show(const char* tag, DWORD cch){
    static char b[DSZ];
    for(int i=0;i<DSZ;i++) b[i]=PB;
    GUID g = {0xDEADBEEF,0x1234,0x5678,{0x9A,0xBC,0xDE,0xF0,0x11,0x22,0x33,0x44}};
    DWORD r = A(&g,b,cch);
    printf("%-22s cch=%-12u ret=%-4u buf=[", tag, cch, r);
    for(int i=0;i<44 && i<DSZ;i++){
        unsigned char c=(unsigned char)b[i];
        putchar(c==0?'.':(c==PB?'-':c));
    }
    printf("]\n");
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"iphlpapi.dll");
    A = (FNA)GetProcAddress(h,"ConvertGuidToStringA");
    W = (FNW)GetProcAddress(h,"ConvertGuidToStringW");
    if(!A){ printf("no ConvertGuidToStringA export\n"); return 1; }
    printf("A=%p  W=%p\n\n", (void*)A, (void*)W);

    printf("---- length regimes ('.' = NUL, '-' = untouched 0x2A) ----\n");
    show("full",        64);
    show("exact fit",   39);
    show("one short",   38);
    show("truncating",  20);
    show("cch 2",        2);
    show("cch 1",        1);
    show("cch 0",        0);
    show("absurd 0x8..", 0x80000000u);
    show("absurd max",   0xFFFFFFFFu);
    show("0x7FFFFFFF",   0x7FFFFFFFu);

    printf("\n---- NULL handling ----\n");
    {
        static char b[DSZ]; for(int i=0;i<DSZ;i++) b[i]=PB;
        printf("Guid NULL      -> %u, buf[0]=%02X\n", A(NULL,b,64), (unsigned char)b[0]);
        printf("String NULL    -> %u\n", A(&(GUID){0},NULL,64));
        printf("both NULL cch0 -> %u\n", A(NULL,NULL,0));
    }

    /* ---- do A and W produce the same characters? 200k random (GUID, cch) pairs ---- */
    printf("\n---- A vs W, character for character ----\n");
    {
        unsigned long sd=0x203203u;
        int diff=0, retdiff=0, checked=0;
        static char a[DSZ]; static wchar_t w[DSZ];
        for(int t=0;t<200000;++t){
            GUID g; unsigned char* p=(unsigned char*)&g;
            for(int i=0;i<16;i++){ sd=sd*1103515245u+12345u; p[i]=(unsigned char)(sd>>16); }
            sd=sd*1103515245u+12345u;
            DWORD cch;
            unsigned s=(sd>>8)%10;
            if(s<5)      cch=(sd>>11)%50;
            else if(s<7) cch=39+((sd>>11)%200);
            else if(s<9) cch=(sd>>11)%3;
            else         cch=0x80000000u+(sd>>11);
            for(int i=0;i<DSZ;i++){ a[i]=PB; w[i]=(wchar_t)PB; }
            DWORD ra=A(&g,a,cch), rw=W(&g,w,cch);
            if(ra!=rw) ++retdiff;
            ++checked;
            for(int i=0;i<DSZ;i++){
                char ac=a[i]; wchar_t wc=w[i];
                char expect = (wc==(wchar_t)PB) ? (char)PB : (char)wc;
                if(ac!=expect){ ++diff; break; }
            }
        }
        printf("%d pairs: %d return-value differences, %d buffer differences\n",
               checked, retdiff, diff);
    }

    /* ---- cost ---- */
    printf("\n---- cost ----\n");
    {
        LARGE_INTEGER f,s,e; QueryPerformanceFrequency(&f);
        SetThreadAffinityMask(GetCurrentThread(),(DWORD_PTR)1<<2);
        SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS);
        static char b[DSZ];
        GUID g = {0xDEADBEEF,0x1234,0x5678,{0x9A,0xBC,0xDE,0xF0,0x11,0x22,0x33,0x44}};
        volatile DWORD sink=0;
        const int N=200000;
        for(int i=0;i<1000;i++) sink^=A(&g,b,64);
        double best=1e300;
        for(int t=0;t<7;t++){
            QueryPerformanceCounter(&s);
            for(int i=0;i<N;i++) sink^=A(&g,b,64);
            QueryPerformanceCounter(&e);
            double ns=(double)(e.QuadPart-s.QuadPart)*1e9/(double)f.QuadPart/N;
            if(ns<best) best=ns;
        }
        printf("ConvertGuidToStringA, cch 64: %.2f ns/call  (sink=%u)\n", best, sink);
    }
    return 0;
}
