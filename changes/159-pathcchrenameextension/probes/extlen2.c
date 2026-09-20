/* probes/extlen2.c -- the extension-length limit: does it count the leading dot, and do 159 and
 * 160 agree? And does this implementation already know about it?
 *
 * extlen.c established that the boundary is at an extension of 257 characters, in every run,
 * independent of the path length and of cch: 256 is accepted, 257 is E_INVALIDARG. Since every
 * extension it built began with a dot, that measurement cannot tell "total length <= 256" from
 * "body length <= 255". This asks both forms.
 *
 * It also runs OUR implementation beside the export, because the live harness caps its extensions
 * at 24 characters and therefore says nothing about this at all.
 */
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern long wia_pathcchrenameext(wchar_t*, size_t, const wchar_t*);
extern long wia_pathcchaddext   (wchar_t*, size_t, const wchar_t*);

typedef HRESULT (WINAPI *fnR)(PWSTR, size_t, PCWSTR);

#define CAP 1200
static wchar_t b1[CAP], b2[CAP], ext[CAP];

static void mk(wchar_t* b, int plen){
    int i; for(i=0;i<CAP;++i) b[i]=0x2A2A;
    for(i=0;i<plen;++i) b[i]=L'a';
    b[plen]=0;
}
/* total = the extension's whole length; dot = 1 to start it with '.' */
static void mkext(int total, int dot){
    int i,k=0;
    if(dot) ext[k++]=L'.';
    for(i=k;i<total;++i) ext[i]=L'x';
    ext[total]=0;
}

static void row(fnR f, const char* tag, int plen, size_t cch, int total, int dot){
    HRESULT a; long b;
    mk(b1,plen); mk(b2,plen); mkext(total,dot);
    a = f(b1,cch,ext);
    b = wia_pathcchrenameext(b2,cch,ext);
    printf("  %-22s ext total=%-4d dot=%d  live=%08lX  ours=%08lX  %s%s\n",
           tag, total, dot, (unsigned long)a, (unsigned long)b,
           (a==(HRESULT)b)?"":"<<< CODE DIFFERS",
           (a==(HRESULT)b && memcmp(b1,b2,sizeof b1))?"<<< BUFFER DIFFERS":"");
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    fnR f  = h ? (fnR)GetProcAddress(h, "PathCchRenameExtension") : NULL;
    fnR fa = h ? (fnR)GetProcAddress(h, "PathCchAddExtension")    : NULL;
    int t;
    if(!f||!fa){ printf("cannot resolve\n"); return 2; }

    printf("(1) WITH a leading dot -- total length across the boundary\n");
    for(t=254;t<=259;++t) row(f,"with dot",6,1000,t,1);

    printf("\n(2) WITHOUT a leading dot -- if the limit counted the BODY, this boundary moves by one\n");
    for(t=254;t<=259;++t) row(f,"no dot",6,1000,t,0);

    printf("\n(3) the boundary is independent of the path and of cch\n");
    row(f,"plen 250",250,1000,256,1);  row(f,"plen 250",250,1000,257,1);
    row(f,"cch 20",    6,  20,256,1);  row(f,"cch 20",    6,  20,257,1);
    row(f,"cch minimal",6,   7,256,1); row(f,"cch minimal",6,   7,257,1);

    printf("\n(4) does the SIBLING, PathCchAddExtension (change 160), use the same limit?\n");
    for(t=255;t<=258;++t){
        HRESULT a; long b;
        mk(b1,6); mk(b2,6); mkext(t,1);
        a = fa(b1,1000,ext);
        b = wia_pathcchaddext(b2,1000,ext);
        printf("  PathCchAddExtension    ext total=%-4d       live=%08lX  ours=%08lX  %s\n",
               t, (unsigned long)a, (unsigned long)b, (a==(HRESULT)b)?"":"<<< CODE DIFFERS");
    }
    printf("\n(5) ORDERING: for PathCchAddExtension, does the over-long extension beat S_FALSE?\n"
           "    The path below ALREADY has an extension, so S_FALSE is on the table. 160's header\n"
           "    says the S_FALSE check runs after the validation, which would make E_INVALIDARG win\n"
           "    -- but that ordering was written before this length rule was known, so it is asked\n"
           "    rather than assumed.\n");
    {
        int lens[4] = { 4, 255, 256, 257 };
        int i;
        for(i=0;i<4;++i){
            HRESULT a; long b;
            int k;
            for(k=0;k<CAP;++k){ b1[k]=0x2A2A; b2[k]=0x2A2A; }
            /* "aaa.txt" -- it already has an extension */
            for(k=0;k<3;++k){ b1[k]=L'a'; b2[k]=L'a'; }
            b1[3]=b2[3]=L'.'; b1[4]=b2[4]=L't'; b1[5]=b2[5]=L'x';
            b1[6]=b2[6]=L't'; b1[7]=b2[7]=0;
            mkext(lens[i],1);
            a = fa(b1,1000,ext);
            b = wia_pathcchaddext(b2,1000,ext);
            printf("  path has an extension   ext total=%-4d       live=%08lX  ours=%08lX  %s\n",
                   lens[i], (unsigned long)a, (unsigned long)b, (a==(HRESULT)b)?"":"<<< CODE DIFFERS");
        }
    }

    printf("\n0x80070057 = E_INVALIDARG, 0x800700CE = ERROR_FILENAME_EXCED_RANGE,\n"
           "0x8007007A = STRSAFE_E_INSUFFICIENT_BUFFER, 0x00000001 = S_FALSE.\n");
    return 0;
}
