/* RtlIsTextUnicode, round 3 -- pin the two predicates rounds 1-2 could not explain, and find the
   prefix cap.

   Round 2 settled:
     * COST SATURATES at ~735 ns: 16 B -> 56 ns, 256 B -> 368 ns, 508 B -> 719 ns, and then FLAT
       from 1 KB all the way to 128 KB. So ntdll scans a BOUNDED PREFIX at ~1.4 ns/byte and stops.
       That is what makes the target worth converting: a vectorized version wins on every size at
       or above the cap, not just on small buffers.
     * CONTROLS is exactly {U+0009, U+000A, U+000D, U+0020, U+3000}, and ONE occurrence is enough.
     * ILLEGAL_CHARS never fires for a single substituted unit at positions 0..3, yet the buffer
       FE FF 00 61 sets it -- so it is a property of the whole buffer, not a character class.
     * ODD_LENGTH == "size is odd"; NULL_BYTES == "a 0x00 byte is present".
     * STATISTICS over 64 IDENTICAL units is set exactly when low(c) > 3*high(c) -- 10965 values in
       85 runs, boundary low = 3*high+1 -- but a SINGLE 'A' among 39 U+0500 units also sets it,
       which no sum-based reading of that boundary explains.
     * ASCII16 is set for 200 units of 'a' and for the two units 'a','b', but NOT for 200 units of
       a..z rotating. Neither "all < 0x80" nor "length" alone explains that.

   Round 3 asks exactly those:
     A. where is the prefix cap?
     B. what is ASCII16's predicate?  (sweep distinct-value count, period, position of the first
        difference, and length together)
     C. what is STATISTICS actually counting?
     D. what makes ILLEGAL_CHARS fire?
   Build: cl /nologo /O2 itu3.c && itu3.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOLEAN (__stdcall *F)(const void*, int, int*);
static F itu;
static wchar_t wbuf[70000];
static int ask(const void* b,int sz,int flag){ int l=flag; return itu(b,sz,&l)?1:0; }
static int flags_of(const void* b,int sz){ int l=-1; itu(b,sz,&l); return l; }

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    itu=(F)GetProcAddress(h,"RtlIsTextUnicode");
    if(!itu){ printf("missing export\n"); return 1; }

    printf("=== A. where is the prefix cap? ===\n");
    {
        /* first K units are plain 'a'; the rest are U+FFFF, which (round 2) suppresses STATISTICS.
           The K at which STATISTICS flips is the number of units ntdll actually looks at. */
        for(int i=0;i<70000;i++) wbuf[i]=0xFFFF;
        int prev=-1;
        for(int K=0;K<=1200;K++){
            if(K) wbuf[K-1]=L'a';
            int s = ask(wbuf,4096,0x0002);
            if(s!=prev){ printf("    K=%-5d (leading 'a' units, buffer 2048 units) STATISTICS=%d\n",K,s); prev=s; }
        }
        /* the mirror: first K units are U+FFFF, the rest plain 'a' */
        for(int i=0;i<70000;i++) wbuf[i]=L'a';
        prev=-1;
        for(int K=0;K<=1200;K++){
            if(K) wbuf[K-1]=0xFFFF;
            int s = ask(wbuf,4096,0x0002);
            if(s!=prev){ printf("    K=%-5d (leading U+FFFF units)              STATISTICS=%d\n",K,s); prev=s; }
        }
        /* and for ASCII16, whose predicate is still unknown */
        for(int i=0;i<70000;i++) wbuf[i]=L'a';
        prev=-1;
        for(int K=0;K<=1200;K++){
            if(K) wbuf[K-1]=0x0500;
            int s = ask(wbuf,4096,0x0001);
            if(s!=prev){ printf("    K=%-5d (leading U+0500 units)              ASCII16=%d\n",K,s); prev=s; }
        }
    }

    printf("\n=== B. ASCII16's predicate ===\n");
    {
        printf("    (b1) N units of 'a', N = 1..24: ");
        for(int N=1;N<=24;N++){ for(int i=0;i<N;i++) wbuf[i]=L'a'; printf("%d",ask(wbuf,N*2,0x0001)); }
        printf("\n");
        printf("    (b2) N units alternating 'a','b': ");
        for(int N=1;N<=24;N++){ for(int i=0;i<N;i++) wbuf[i]=(wchar_t)(L'a'+(i&1)); printf("%d",ask(wbuf,N*2,0x0001)); }
        printf("\n");
        printf("    (b3) N units rotating a..z:       ");
        for(int N=1;N<=24;N++){ for(int i=0;i<N;i++) wbuf[i]=(wchar_t)(L'a'+(i%26)); printf("%d",ask(wbuf,N*2,0x0001)); }
        printf("\n");
        printf("    (b4) 200 units of 'a' with ONE unit changed to 'b' at position p:\n        ");
        for(int p=0;p<40;p++){
            for(int i=0;i<200;i++) wbuf[i]=L'a';
            wbuf[p]=L'b';
            printf("%d",ask(wbuf,400,0x0001));
        }
        printf("   (p = 0..39)\n");
        printf("    (b5) 200 units, first D of them distinct (a,b,c,...), rest 'a':\n        ");
        for(int D=0;D<=30;D++){
            for(int i=0;i<200;i++) wbuf[i]=L'a';
            for(int i=0;i<D;i++) wbuf[i]=(wchar_t)(L'a'+i);
            printf("%d",ask(wbuf,400,0x0001));
        }
        printf("   (D = 0..30)\n");
        printf("    (b6) real English sentence, wide:\n");
        {
            const wchar_t* s = L"The quick brown fox jumps over the lazy dog. Pack my box with five dozen liquor jugs.";
            int n=(int)wcslen(s);
            for(int i=0;i<n;i++) wbuf[i]=s[i];
            printf("        ASCII16=%d  flags=%08X  (%d units)\n", ask(wbuf,n*2,0x0001), flags_of(wbuf,n*2), n);
        }
    }

    printf("\n=== C. what is STATISTICS counting? ===\n");
    {
        /* C1: two values mixed -- k units of X among 64, rest Y. Sweep X and Y over high bytes. */
        printf("    (c1) k units of U+0041 among 64, filler U+hh00, boundary k:\n");
        for(int hb=1; hb<=8; ++hb){
            unsigned fill = (unsigned)hb<<8;
            int first=-1;
            for(int k=0;k<=64;k++){
                for(int j=0;j<64;j++) wbuf[j]=(wchar_t)((j<k)?0x0041:fill);
                if(ask(wbuf,128,0x0002)){ first=k; break; }
            }
            printf("        filler U+%04X -> STATISTICS first set at k=%d\n", fill, first);
        }
        /* C2: identical units again, but measure where the boundary sits per high byte, to
               confirm the low > 3*high reading and see if it is really a ratio */
        printf("    (c2) identical units: lowest low-byte that sets STATISTICS, per high byte:\n        ");
        for(int hb=0; hb<=20; ++hb){
            int lo=-1;
            for(int l=0;l<256;l++){
                unsigned c=((unsigned)hb<<8)|(unsigned)l;
                for(int j=0;j<64;j++) wbuf[j]=(wchar_t)c;
                if(ask(wbuf,128,0x0002)){ lo=l; break; }
            }
            printf("h=%02X:%02X ", hb, lo);
        }
        printf("\n");
        /* C3: does the unit COUNT change the identical-unit boundary? */
        printf("    (c3) identical U+0503 (high 5, low 3 -> below the boundary), unit count 2..40:\n        ");
        for(int N=2;N<=40;N++){ for(int j=0;j<N;j++) wbuf[j]=0x0503; printf("%d",ask(wbuf,N*2,0x0002)); }
        printf("\n");
        /* C4: split the difference -- half U+0500, half U+0041, vs other ratios */
        printf("    (c4) N units: n of U+00FF, 64-n of U+FF00:\n        ");
        for(int n=0;n<=64;n+=2){
            for(int j=0;j<64;j++) wbuf[j]=(wchar_t)((j<n)?0x00FF:0xFF00);
            printf("%d",ask(wbuf,128,0x0002));
        }
        printf("   (n = 0,2,..64)\n");
    }

    printf("\n=== D. what makes ILLEGAL_CHARS fire? ===\n");
    {
        struct { const char* what; const unsigned char* b; int sz; } T[] = {
            {"FE FF 00 61",        (const unsigned char*)"\xFE\xFF\x00\x61", 4},
            {"FF FE 61 00",        (const unsigned char*)"\xFF\xFE\x61\x00", 4},
            {"FE FF only",         (const unsigned char*)"\xFE\xFF", 2},
            {"61 00 FE FF",        (const unsigned char*)"\x61\x00\xFE\xFF", 4},
            {"61 00 FF FE",        (const unsigned char*)"\x61\x00\xFF\xFE", 4},
            {"EF BB BF 61",        (const unsigned char*)"\xEF\xBB\xBF\x61", 4},
            {"00 00 00 00",        (const unsigned char*)"\x00\x00\x00\x00", 4},
            {"FF FF FF FF",        (const unsigned char*)"\xFF\xFF\xFF\xFF", 4},
        };
        for(int i=0;i<8;i++){
            int l=0x0100; BOOLEAN r=itu(T[i].b,T[i].sz,&l);
            printf("    %-16s ILLEGAL_CHARS asked -> BOOL=%d out=%04X ; all flags=%08X\n",
                   T[i].what, r, l, flags_of(T[i].b,T[i].sz));
        }
        /* sweep a single unit at position 0 of a 32-unit buffer, asking ONLY illegal */
        for(int k=0;k<32;k++) wbuf[k]=L'q';
        int n=0;
        for(unsigned c=0;c<0x10000;c++){
            wbuf[0]=(wchar_t)c;
            if(ask(wbuf,64,0x0100)) { if(n<10) printf("    pos0 U+%04X sets ILLEGAL_CHARS\n",c); ++n; }
        }
        wbuf[0]=L'q';
        printf("    %d of 65536 units at position 0 set ILLEGAL_CHARS in a 32-unit buffer\n", n);
        /* short buffers */
        for(int sz=2; sz<=8; sz+=2){
            for(int k=0;k<4;k++) wbuf[k]=0xFFFE;
            printf("    %d bytes of U+FFFE: flags=%08X\n", sz, flags_of(wbuf,sz));
            for(int k=0;k<4;k++) wbuf[k]=0xFFFF;
            printf("    %d bytes of U+FFFF: flags=%08X\n", sz, flags_of(wbuf,sz));
        }
    }
    return 0;
}
