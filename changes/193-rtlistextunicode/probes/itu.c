/* Derive ntdll!RtlIsTextUnicode.

   Why this target: at 722 ns for a 508-byte buffer it is the slowest routine found in the whole
   headroom survey -- about 0.7 GB/s, i.e. roughly 1.4 ns PER BYTE. Everything else in this repo
   that scans memory runs 20-80 GB/s.

   What makes it hard: it is a HEURISTIC with a bitmask of independent tests, and the return value
   is "did the tests that were asked for pass", while *lpi is rewritten to the set that actually
   fired. Nothing about it can be guessed -- MSDN's description is famously incomplete about which
   tests run when lpi is NULL and how the ODD_LENGTH / signature cases interact. So this probe
   asks, one flag at a time:

     1. With lpi == NULL, which tests are performed, and what decides the BOOL?
     2. For each individual flag, on a controlled buffer, does it fire when it should?
     3. What exactly is the STATISTICS heuristic counting, and what is its threshold?
     4. What does CONTROLS count, and which control characters?
     5. What is ILLEGAL_CHARS -- which code units are "illegal"?
     6. ODD_LENGTH: does an odd byte count suppress the other tests, or add to them?
     7. Size 0 and size 1 buffers.
     8. Is the answer affected by anything other than the buffer bytes (locale, ANSI code page)?

   Build: cl /nologo /O2 itu.c && itu.exe
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#ifndef IS_TEXT_UNICODE_ASCII16
#define IS_TEXT_UNICODE_ASCII16               0x0001
#define IS_TEXT_UNICODE_REVERSE_ASCII16       0x0010
#define IS_TEXT_UNICODE_STATISTICS            0x0002
#define IS_TEXT_UNICODE_REVERSE_STATISTICS    0x0020
#define IS_TEXT_UNICODE_CONTROLS              0x0004
#define IS_TEXT_UNICODE_REVERSE_CONTROLS      0x0040
#define IS_TEXT_UNICODE_SIGNATURE             0x0008
#define IS_TEXT_UNICODE_REVERSE_SIGNATURE     0x0080
#define IS_TEXT_UNICODE_ILLEGAL_CHARS         0x0100
#define IS_TEXT_UNICODE_ODD_LENGTH            0x0200
#define IS_TEXT_UNICODE_DBCS_LEADBYTE         0x0400
#define IS_TEXT_UNICODE_NULL_BYTES            0x1000
#define IS_TEXT_UNICODE_UNICODE_MASK          0x000F
#define IS_TEXT_UNICODE_REVERSE_MASK          0x00F0
#define IS_TEXT_UNICODE_NOT_UNICODE_MASK      0x0F00
#define IS_TEXT_UNICODE_NOT_ASCII_MASK        0xF000
#endif

typedef BOOLEAN (__stdcall *F)(const void*, int, int*);
static F itu;

static const struct { int bit; const char* name; } FLAGS[] = {
    { 0x0001, "ASCII16" },        { 0x0002, "STATISTICS" },
    { 0x0004, "CONTROLS" },       { 0x0008, "SIGNATURE" },
    { 0x0010, "REV_ASCII16" },    { 0x0020, "REV_STATISTICS" },
    { 0x0040, "REV_CONTROLS" },   { 0x0080, "REV_SIGNATURE" },
    { 0x0100, "ILLEGAL_CHARS" },  { 0x0200, "ODD_LENGTH" },
    { 0x0400, "DBCS_LEADBYTE" },  { 0x0800, "(0x0800)" },
    { 0x1000, "NULL_BYTES" },     { 0x2000, "(0x2000)" },
    { 0x4000, "(0x4000)" },       { 0x8000, "(0x8000)" },
};

static void names(int m, char* out, int cap){
    out[0]=0;
    for(int i=0;i<16;i++) if(m & FLAGS[i].bit){
        if(out[0]) strncat(out,"|",cap-1-(int)strlen(out));
        strncat(out,FLAGS[i].name,cap-1-(int)strlen(out));
    }
    if(!out[0]) strncat(out,"(none)",cap-1);
}

static void show(const char* what, const void* buf, int sz){
    int lpi = -1;                                   /* ask for EVERYTHING */
    BOOLEAN r1 = itu(buf, sz, &lpi);
    BOOLEAN r2 = itu(buf, sz, NULL);
    char nm[400]; names(lpi, nm, sizeof nm);
    printf("  %-34s sz=%-5d all->%d out=%08X %-52s   NULL->%d\n", what, sz, r1, lpi, nm, r2);
}

/* ask for exactly one flag and report the pair (return, *lpi) */
static void one_flag(const char* what, const void* buf, int sz){
    printf("  %-30s sz=%-5d : ", what, sz);
    for(int i=0;i<16;i++){
        int lpi = FLAGS[i].bit;
        BOOLEAN r = itu(buf, sz, &lpi);
        if(r || lpi) printf("%s=%d/%04X ", FLAGS[i].name, r, lpi);
    }
    printf("\n");
}

static wchar_t wbuf[600];
static char    abuf[1200];

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    itu = (F)GetProcAddress(h,"RtlIsTextUnicode");
    if(!itu){ printf("missing export\n"); return 1; }

    printf("=== 1. what does lpi == NULL test, and what decides the BOOL? ===\n");
    {
        for(int i=0;i<200;i++) wbuf[i]=(wchar_t)(L'a'+(i%26));
        show("plain ASCII-range UTF-16", wbuf, 400);
        strcpy(abuf, "This is ordinary ANSI English text, quite long, with spaces.");
        show("ANSI text", abuf, (int)strlen(abuf));
        for(int i=0;i<200;i++) wbuf[i]=(wchar_t)(0x0500+(i%200));
        show("non-ASCII UTF-16 (U+0500..)", wbuf, 400);
        { unsigned char b[]={0xFF,0xFE,'a',0,'b',0,'c',0};
          show("BOM FF FE + wide text", b, 8); }
        { unsigned char b[]={0xFE,0xFF,0,'a',0,'b',0,'c'};
          show("reverse BOM FE FF + BE text", b, 8); }
        { unsigned char b[]={'a',0,'b',0,'c',0};
          show("wide text, no BOM", b, 6); }
        { unsigned char b[]={0,'a',0,'b',0,'c'};
          show("big-endian wide text, no BOM", b, 6); }
        show("empty", wbuf, 0);
        show("one byte", "A", 1);
        show("two bytes 'A' 0", "A\0", 2);
        show("three bytes (odd)", "A\0B", 3);
    }

    printf("\n=== 2. each flag asked for ALONE ===\n");
    {
        for(int i=0;i<100;i++) wbuf[i]=(wchar_t)(L'a'+(i%26));
        one_flag("ASCII-range UTF-16", wbuf, 200);
        strcpy(abuf,"Ordinary ANSI sentence text here.");
        one_flag("ANSI text", abuf, (int)strlen(abuf));
        { unsigned char b[]={0xFF,0xFE,'a',0}; one_flag("BOM + 1 wide char", b, 4); }
        { unsigned char b[]={0xFE,0xFF,0,'a'}; one_flag("reverse BOM", b, 4); }
        one_flag("odd length", "A\0B", 3);
    }

    printf("\n=== 3. STATISTICS: what is counted, and where is the threshold? ===\n");
    {
        /* buffers of N wide chars, all the SAME unit, swept over the unit value */
        static const unsigned short probe[] = {0x0020,0x0041,0x0061,0x00E9,0x0100,0x0401,
                                               0x2000,0x3042,0xFF41,0x0009,0x000A,0x0000};
        for(int i=0;i<12;i++){
            for(int k=0;k<64;k++) wbuf[k]=probe[i];
            int lpi = IS_TEXT_UNICODE_STATISTICS;
            BOOLEAN r = itu(wbuf,128,&lpi);
            int lpr = IS_TEXT_UNICODE_REVERSE_STATISTICS;
            BOOLEAN rr = itu(wbuf,128,&lpr);
            printf("    64 x U+%04X : STATISTICS=%d  REVERSE_STATISTICS=%d\n", probe[i], r, rr);
        }
        /* mix: how many ASCII-range units are needed out of 64? */
        printf("    --- mixing U+0041 with U+0500, 64 units total ---\n");
        for(int n=0;n<=64;n+=4){
            for(int k=0;k<64;k++) wbuf[k] = (k<n) ? 0x0041 : 0x0500;
            int lpi = IS_TEXT_UNICODE_STATISTICS;
            BOOLEAN r = itu(wbuf,128,&lpi);
            printf("      %2d/64 ASCII -> STATISTICS=%d\n", n, r);
        }
    }

    printf("\n=== 4. CONTROLS: which units count as controls? ===\n");
    {
        for(int c=0;c<0x40;c++){
            for(int k=0;k<8;k++) wbuf[k]=(wchar_t)c;
            int lpi = IS_TEXT_UNICODE_CONTROLS;
            BOOLEAN r = itu(wbuf,16,&lpi);
            if(r) printf("    U+%04X counts as a CONTROL\n", c);
        }
        /* one control in an otherwise plain buffer */
        for(int k=0;k<32;k++) wbuf[k]=L'a';
        printf("    --- one control among 32 'a' ---\n");
        for(int c=0;c<0x20;c++){
            wbuf[5]=(wchar_t)c;
            int lpi = IS_TEXT_UNICODE_CONTROLS;
            BOOLEAN r = itu(wbuf,64,&lpi);
            if(r) printf("      U+%04X alone triggers CONTROLS\n", c);
            wbuf[5]=L'a';
        }
    }

    printf("\n=== 5. ILLEGAL_CHARS: which units are illegal? ===\n");
    {
        for(int k=0;k<32;k++) wbuf[k]=L'a';
        int shown=0;
        for(unsigned c=0;c<0x10000 && shown<40;c++){
            wbuf[5]=(wchar_t)c;
            int lpi = IS_TEXT_UNICODE_ILLEGAL_CHARS;
            BOOLEAN r = itu(wbuf,64,&lpi);
            if(r){ printf("    U+%04X is ILLEGAL\n", c); ++shown; }
            wbuf[5]=L'a';
        }
        /* count them all */
        int n=0;
        for(unsigned c=0;c<0x10000;c++){
            wbuf[5]=(wchar_t)c;
            int lpi = IS_TEXT_UNICODE_ILLEGAL_CHARS;
            if(itu(wbuf,64,&lpi)) ++n;
        }
        wbuf[5]=L'a';
        printf("    %d of 65536 code units are ILLEGAL\n", n);
    }

    printf("\n=== 6. NULL_BYTES ===\n");
    {
        for(int k=0;k<32;k++) wbuf[k]=L'a';
        { int lpi=IS_TEXT_UNICODE_NULL_BYTES; printf("    32 x 'a' (high bytes are 0): NULL_BYTES=%d\n", itu(wbuf,64,&lpi)); }
        for(int k=0;k<32;k++) wbuf[k]=0x0500;
        { int lpi=IS_TEXT_UNICODE_NULL_BYTES; printf("    32 x U+0500 (no zero byte):  NULL_BYTES=%d\n", itu(wbuf,64,&lpi)); }
        strcpy(abuf,"plain ansi text with no zero bytes at all");
        { int lpi=IS_TEXT_UNICODE_NULL_BYTES; printf("    ANSI text:                   NULL_BYTES=%d\n", itu(abuf,(int)strlen(abuf),&lpi)); }
    }

    printf("\n=== 7. ODD_LENGTH interaction ===\n");
    {
        for(int i=0;i<50;i++) wbuf[i]=(wchar_t)(L'a'+(i%26));
        for(int sz=95; sz<=101; ++sz) show("ASCII-range wide, varying size", wbuf, sz);
    }

    printf("\n=== 8. is it affected by anything outside the buffer? ===\n");
    {
        for(int i=0;i<100;i++) wbuf[i]=(wchar_t)(L'a'+(i%26));
        int a=-1; itu(wbuf,200,&a);
        UINT oldcp = GetACP();
        printf("    ACP=%u, result=%08X\n", oldcp, a);
        printf("    (the DBCS_LEADBYTE flag is the one that could depend on the ANSI code page;\n"
               "     whether it is ever set here is visible above)\n");
    }
    return 0;
}
