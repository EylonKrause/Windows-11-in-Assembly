/* probes/maxpath.c -- PathCchRenameExtension has a THIRD size failure the contract missed.
 *
 * The live harness found 355 of 12000 cases differing, every one of them with an input length of
 * 258 or 259 -- inside the documented "length <= 259" limit -- where the export answered
 * 0x800700CE (ERROR_FILENAME_EXCED_RANGE) and this implementation answered either
 * 0x8007007A (STRSAFE_E_INSUFFICIENT_BUFFER) or, with a generous cch, S_OK.
 *
 * 0x800700CE is documented for the SIBLING function, change 160 PathCchAddExtension: "either
 * STRSAFE_E_INSUFFICIENT_BUFFER when cch-1 is smaller than the result, or 0x800700CE when cch was
 * big enough and only the 259-character MAX_PATH limit was breached". Change 159's contract records
 * the 0x8007007A half and a limit on the INPUT length, and has nothing about the RESULT length.
 *
 * Three things to settle, and only the first is visible in the harness output:
 *   (1) does a result longer than 259 fail even when the input was legal and cch is generous?
 *   (2) which wins when BOTH the cch and the 259 limits are breached?
 *   (3) what is left in the buffer in each case -- untouched, fully written, or 160's peculiar
 *       "terminator where the dot would go, then the body, clamped" truncating write?
 * (3) is the one that decides whether the fix is a returned code or a code AND a write.
 */
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef HRESULT (WINAPI *fnR)(PWSTR, size_t, PCWSTR);

#define CAP 400
static wchar_t buf[CAP];

/* build a path of `len` characters, of the form "C:\aaa...a" with no dot, or with one if dot>=0 */
static void mk(int len, int dot){
    int i;
    for(i=0;i<CAP;++i) buf[i]=0x2A2A;
    buf[0]=L'C'; buf[1]=L':'; buf[2]=L'\\';
    for(i=3;i<len;++i) buf[i]=L'a';
    if(dot>=3 && dot<len) buf[dot]=L'.';
    buf[len]=0;
}

static void show(fnR f, int len, int dot, size_t cch, const wchar_t* ext){
    HRESULT r; int i, last=-1, nz=0;
    mk(len,dot);
    r = f(buf, cch, ext);
    for(i=0;i<CAP;++i) if(buf[i]!=0x2A2A) last=i;
    for(i=0;i<=last;++i) if(buf[i]==0) ++nz;
    printf("  len=%-4d dot=%-5s cch=%-6llu ext='%-6ls' -> %08lX  touched..%-4d NULs=%d  ",
           len, dot<0?"none":"yes", (unsigned long long)cch, ext, (unsigned long)r, last, nz);
    /* print the tail of the buffer around the interesting region */
    if(last>=0){
        int from = last-9; if(from<0) from=0;
        printf("tail[%d..%d]:",from,last);
        for(i=from;i<=last;++i){
            if(buf[i]==0) printf(" 0000");
            else if(buf[i]==0x2A2A) printf(" ****");
            else printf(" %04X", (unsigned)buf[i]);
        }
    }
    printf("\n");
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    fnR f = h ? (fnR)GetProcAddress(h, "PathCchRenameExtension") : NULL;
    if(!f){ printf("cannot resolve PathCchRenameExtension\n"); return 2; }

    printf("(1) input legal (<=259), cch GENEROUS, result length crossing 259\n");
    printf("    a path with no dot gains one, so the result is len + strlen(ext).\n");
    show(f,251,-1,CAP-1,L".obj");   /* 255 */
    show(f,254,-1,CAP-1,L".obj");   /* 258 */
    show(f,255,-1,CAP-1,L".obj");   /* 259  <- the last legal result */
    show(f,256,-1,CAP-1,L".obj");   /* 260  <- first illegal result  */
    show(f,259,-1,CAP-1,L".obj");   /* 263 */
    show(f,259,-1,CAP-1,L"");       /* result 259: empty ext truncates at the insertion point */

    printf("\n(1b) the same, but the path HAS a dot, so the extension is replaced not appended\n");
    show(f,259,255,CAP-1,L".obj");  /* 255 + 4 = 259 -- legal result from a 259 input */
    show(f,259,250,CAP-1,L".obj");  /* 250 + 4 = 254 */
    show(f,259,255,CAP-1,L".objxx");/* 255 + 6 = 261 -- illegal result, legal input */

    printf("\n(2) BOTH limits breached -- which code wins?\n");
    show(f,256,-1,8,L".obj");       /* result 260 > 259, and cch far too small */
    show(f,256,-1,260,L".obj");     /* result 260 > 259, cch one short as well  */
    show(f,256,-1,261,L".obj");     /* result 260 > 259, cch large enough       */

    printf("\n(3) cch too small ALONE, result within 259 -- the documented 0x8007007A path\n");
    show(f,6,-1,8,L".obj");
    show(f,6,-1,9,L".obj");
    show(f,6,-1,11,L".obj");

    printf("\n(4) the input-length limit itself, for contrast (E_INVALIDARG expected at 260)\n");
    show(f,259,-1,CAP-1,L".o");
    show(f,260,-1,CAP-1,L".o");

    printf("\n**** = untouched poison 0x2A2A. A 'touched' index past the terminator means the\n"
           "routine wrote there. Compare with change 160's truncating write: path[len] = 0,\n"
           "then the body clamped to limit-len-1 characters, then path[limit] = 0.\n");
    return 0;
}
