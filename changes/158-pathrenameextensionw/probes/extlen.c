/* probes/extlen.c: does shlwapi!PathRenameExtensionW have an extension-length limit too?
 *
 * Changes 159 and 160 (the kernelbase PathCch pair) both turned out to reject an extension whose
 * body exceeds 255 characters with E_INVALIDARG, a rule neither contract recorded and neither
 * oracle modelled. 158 is the shlwapi function that does the same job, and its contract says the
 * opposite about validation; "the new extension is NOT validated: 'obj' (no dot) gives
 * 'f.txtobj' -> 'fobj', '.a.b' is taken whole", so it plausibly has no such rule.
 *
 * "Plausibly" is not a measurement. The live harness that covers this export caps its extensions
 * at 24 characters, so it has never been asked. This asks, across the whole range, against both
 * the export and this implementation, and checks the BUFFER as well as the BOOL, 158's documented
 * failure mode is "the destination is left COMPLETELY UNCHANGED", which is the interesting thing to
 * get wrong.
 */
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern int wia_pathrenameextw(wchar_t*, const wchar_t*);
typedef BOOL (WINAPI *fnR)(PWSTR, PCWSTR);

#define CAP 1200
static wchar_t b1[CAP], b2[CAP], ext[CAP];

static void mk(wchar_t* b, int plen, int dot){
    int i; for(i=0;i<CAP;++i) b[i]=0x2A2A;
    for(i=0;i<plen;++i) b[i]=L'a';
    if(dot>=0 && dot<plen) b[dot]=L'.';
    b[plen]=0;
}

static int row(fnR f, int plen, int dot, int total, int quiet){
    BOOL a; int b, bad=0;
    int i;
    mk(b1,plen,dot); mk(b2,plen,dot);
    ext[0]=L'.'; for(i=1;i<total;++i) ext[i]=L'x'; ext[total]=0;
    a = f(b1,ext);
    b = wia_pathrenameextw(b2,ext);
    if((int)a!=b || memcmp(b1,b2,sizeof b1)) bad=1;
    if(!quiet || bad)
        printf("  plen=%-4d dot=%-5s ext total=%-4d result=%-4d  live=%d ours=%d  %s\n",
               plen, dot<0?"none":"yes", total, (dot<0?plen:dot)+total,
               (int)a, b, bad?"<<< DIFFERS":"");
    return bad;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    fnR f = h ? (fnR)GetProcAddress(h, "PathRenameExtensionW") : NULL;
    int t, bad=0;
    if(!f){ printf("cannot resolve PathRenameExtensionW\n"); return 2; }

    printf("(1) a SHORT path, so only the extension length can decide -- every length 0..500\n");
    for(t=0;t<=500;++t) bad += row(f,6,-1,t,1);
    printf("      %d differences over 501 extension lengths\n", bad);

    printf("\n(2) around the 255/256 boundary that 159 and 160 use, printed in full\n");
    for(t=253;t<=259;++t) row(f,6,-1,t,0);

    printf("\n(3) a long path, where the 259 RESULT limit is what should decide\n");
    for(t=1;t<=6;++t) row(f,255,-1,t,0);

    printf("\n(4) replacing an existing extension with a very long one\n");
    row(f,100,96,300,0);
    row(f,100,96,400,0);

    if(!bad) printf("\nNo extension-length limit here: 158 really does accept any length and is\n"
                    "bounded only by the 259-character result, exactly as its contract says.\n");
    else     printf("\n%d DIFFERENCES -- this export has a rule the implementation does not.\n", bad);
    return bad?1:0;
}
