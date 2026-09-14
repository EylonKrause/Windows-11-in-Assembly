/* RtlIsTextUnicode, round 2 -- sharpen the rules round 1 left open, and first decide whether the
   routine is even worth converting.

   Round 1 (itu.c) established:
     * lpi == NULL gives the same BOOL as lpi == -1 on every case tried;
     * ODD_LENGTH is exactly "size is odd", and it vetoes TRUE;
     * NULL_BYTES is "the buffer contains a 0x00 byte";
     * CONTROLS fires on a SINGLE U+0009 / U+000A / U+000D among ordinary letters;
     * ILLEGAL_CHARS did NOT fire for any of the 65536 units placed at index 5, yet DID fire for a
       buffer starting FE FF -- so it is position-sensitive, not a character set;
     * ASCII16 was set for a 3-unit buffer but NOT for 48- or 200-unit buffers of the same
       characters -- so something about length or content suppresses it;
     * STATISTICS is NOT the classic "zero bytes at odd offsets" rule: 64 x U+0500, whose zero
       bytes are all at EVEN offsets, set neither STATISTICS nor REVERSE_STATISTICS.

   Round 2 asks:
     Q0. Is the cost proportional to size? If ntdll only inspects a prefix, there is nothing to win
         and this target should be dropped before any more effort goes into it.
     Q1. ASCII16 vs length: sweep the unit count with identical content.
     Q2. STATISTICS: what is actually counted? Sweep one variable at a time.
     Q3. CONTROLS: is U+0020 in the set? Is one occurrence enough? Exact set over 0..0xFFFF.
     Q4. ILLEGAL_CHARS: position sweep for the units that can trigger it.
     Q5. What function of the flag set produces the BOOL?
   Build: cl /nologo /O2 itu2.c && itu2.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOLEAN (__stdcall *F)(const void*, int, int*);
static F itu;
static wchar_t wbuf[70000];

static double freq;
static double now_ns(void){ LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart*1e9/freq; }

static int flags_of(const void* b, int sz){ int l=-1; itu(b,sz,&l); return l; }
static int ask(const void* b, int sz, int flag){ int l=flag; return itu(b,sz,&l)?1:0; }

int main(void){
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); freq=(double)f.QuadPart;
    SetThreadAffinityMask(GetCurrentThread(),(DWORD_PTR)1<<2);
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    itu = (F)GetProcAddress(h,"RtlIsTextUnicode");
    if(!itu){ printf("missing export\n"); return 1; }

    printf("=== Q0. is the cost proportional to size? (this decides whether to continue) ===\n");
    {
        for(int i=0;i<70000;i++) wbuf[i]=(wchar_t)(L'a'+(i%26));
        static const int SZ[] = {16,64,256,508,1024,4096,16384,65536,131072};
        for(int i=0;i<9;i++){
            int sz = SZ[i];
            double best=1e300;
            for(int t=0;t<7;t++){
                double t0=now_ns();
                for(int k=0;k<2000;k++){ int l=-1; itu(wbuf,sz,&l); }
                double per=(now_ns()-t0)/2000.0;
                if(per<best) best=per;
            }
            printf("    size %7d bytes -> %10.2f ns   (%6.3f ns/byte, %5.2f GB/s)\n",
                   sz, best, best/sz, sz/best);
        }
    }

    printf("\n=== Q1. ASCII16 vs unit count, identical content ===\n");
    {
        for(int i=0;i<70000;i++) wbuf[i]=L'a';
        printf("    all 'a':      ");
        for(int n=1;n<=40;n++){ printf("%d", ask(wbuf,n*2,0x0001)); }
        printf("   (unit counts 1..40)\n");
        for(int n=1;n<=12;n++){
            int nn = n*64;
            printf("    %5d units: ASCII16=%d flags=%08X\n", nn, ask(wbuf,nn*2,0x0001), flags_of(wbuf,nn*2));
        }
        /* does a specific character matter? */
        for(int i=0;i<200;i++) wbuf[i]=(wchar_t)(L'a'+(i%26));
        printf("    200 units of a..z rotating: ASCII16=%d\n", ask(wbuf,400,0x0001));
        for(int i=0;i<200;i++) wbuf[i]=L'a';
        printf("    200 units of 'a':           ASCII16=%d\n", ask(wbuf,400,0x0001));
        for(int i=0;i<200;i++) wbuf[i]=(wchar_t)(0x00E9);
        printf("    200 units of U+00E9:        ASCII16=%d\n", ask(wbuf,400,0x0001));
    }

    printf("\n=== Q2. STATISTICS: sweep one variable at a time ===\n");
    {
        /* (a) a single repeated unit, over the whole BMP, 64 units */
        printf("    (a) 64 identical units -- which unit values set STATISTICS?\n");
        int runs=0, first=-1, prev=0, n=0;
        for(unsigned c=0;c<0x10000;c++){
            for(int k=0;k<64;k++) wbuf[k]=(wchar_t)c;
            int s = ask(wbuf,128,0x0002);
            if(s) ++n;
            if(s && !prev){ first=(int)c; ++runs; }
            if(!s && prev) printf("        U+%04X..U+%04X set STATISTICS\n", first, c-1);
            prev=s;
        }
        if(prev) printf("        U+%04X..U+FFFF set STATISTICS\n", first);
        printf("        total %d of 65536 unit values, in %d run(s)\n", n, runs);

        /* (b) threshold: k ASCII units among 64, filler U+0500 and filler U+2020 */
        printf("    (b) threshold, k ASCII 'A' among 64 units\n");
        static const unsigned short FILL[] = {0x0500,0x2020,0x0100,0xFFFF};
        for(int fi=0; fi<4; ++fi){
            printf("        filler U+%04X: ", FILL[fi]);
            for(int k=0;k<=16;k++){
                for(int j=0;j<64;j++) wbuf[j] = (j<k) ? 0x0041 : FILL[fi];
                printf("%d", ask(wbuf,128,0x0002));
            }
            printf("   (k = 0..16 of 64)\n");
        }
        /* (c) does the total length change the threshold? */
        printf("    (c) exactly ONE 'A' among N units of U+0500\n        ");
        for(int N=2;N<=40;N++){
            for(int j=0;j<N;j++) wbuf[j]=0x0500;
            wbuf[0]=0x0041;
            printf("%d", ask(wbuf,N*2,0x0002));
        }
        printf("   (N = 2..40)\n");
    }

    printf("\n=== Q3. CONTROLS: exact set, one occurrence among letters ===\n");
    {
        for(int k=0;k<64;k++) wbuf[k]=L'q';
        int n=0, prev=0, first=-1;
        for(unsigned c=0;c<0x10000;c++){
            wbuf[7]=(wchar_t)c;
            int s = ask(wbuf,128,0x0004);
            if(s) ++n;
            if(s && !prev){ first=(int)c; }
            if(!s && prev) printf("        U+%04X..U+%04X are CONTROLS\n", first, c-1);
            prev=s;
        }
        wbuf[7]=L'q';
        if(prev) printf("        U+%04X..U+FFFF are CONTROLS\n", first);
        printf("        %d of 65536 units trigger CONTROLS from a single occurrence\n", n);
        for(int k=0;k<64;k++) wbuf[k]=0x0020;
        printf("        64 x U+0020 (all spaces): CONTROLS=%d\n", ask(wbuf,128,0x0004));
    }

    printf("\n=== Q4. ILLEGAL_CHARS: which unit at which position? ===\n");
    {
        for(int pos=0;pos<4;pos++){
            for(int k=0;k<32;k++) wbuf[k]=L'q';
            int n=0; int shown=0;
            for(unsigned c=0;c<0x10000;c++){
                wbuf[pos]=(wchar_t)c;
                if(ask(wbuf,64,0x0100)){ ++n; if(shown<8){ printf("        pos %d: U+%04X\n",pos,c); ++shown; } }
            }
            wbuf[pos]=L'q';
            printf("      position %d: %d of 65536 units set ILLEGAL_CHARS\n", pos, n);
        }
    }

    printf("\n=== Q5. what produces the BOOL from the flag set? ===\n");
    {
        struct { const char* what; const unsigned char* b; int sz; } T[] = {
            {"FF FE 'a'0",            (const unsigned char*)"\xFF\xFE\x61\x00", 4},
            {"FE FF 00'a'",           (const unsigned char*)"\xFE\xFF\x00\x61", 4},
            {"'a'0'b'0",              (const unsigned char*)"\x61\x00\x62\x00", 4},
            {"00'a'00'b'",            (const unsigned char*)"\x00\x61\x00\x62", 4},
            {"'a''b''c''d'",          (const unsigned char*)"abcd", 4},
            {"09 00 0A 00",           (const unsigned char*)"\x09\x00\x0A\x00", 4},
            {"FF FE (BOM only)",      (const unsigned char*)"\xFF\xFE", 2},
            {"FF FE 'a'",             (const unsigned char*)"\xFF\xFE\x61", 3},
        };
        for(int i=0;i<8;i++){
            int l=-1; BOOLEAN r = itu(T[i].b,T[i].sz,&l);
            BOOLEAN rn = itu(T[i].b,T[i].sz,NULL);
            printf("    %-22s -> BOOL(all)=%d BOOL(NULL)=%d flags=%08X\n", T[i].what, r, rn, l);
        }
        /* ask for one flag at a time on a buffer whose full flag set is known */
        for(int k=0;k<64;k++) wbuf[k]=(wchar_t)(L'a'+(k%26));
        int full = flags_of(wbuf,128);
        printf("    64 ASCII units: full flags=%08X\n", full);
        for(int bit=0;bit<16;bit++){
            int l = 1<<bit; BOOLEAN r = itu(wbuf,128,&l);
            if(r || l) printf("        ask %04X -> BOOL=%d out=%04X\n", 1<<bit, r, l);
        }
    }
    return 0;
}
