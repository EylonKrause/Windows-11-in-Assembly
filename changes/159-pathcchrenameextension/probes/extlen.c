/* probes/extlen.c -- is there a LENGTH limit on the extension itself?
 *
 * which.c drove a 300-character extension at a 6-character path with a generous cch and got
 * E_INVALIDARG (0x80070057) with the buffer untouched -- not a size failure, not a truncating
 * write, but outright rejection, in a case where the result would merely have been too long.
 * A 4-character extension in the same position gives 0x800700CE instead.
 *
 * So there is a rule about the extension's own length that nothing in this change's contract
 * records, and it has to be pinned before it can be claimed: where exactly is the boundary, is it
 * the extension's length or the result's, and does it depend on cch or on the path at all?
 *
 * This holds the path short and fixed and walks the extension's length, then repeats with two
 * other path lengths to see whether the boundary moves.
 */
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef HRESULT (WINAPI *fnR)(PWSTR, size_t, PCWSTR);

#define CAP 1200
static wchar_t buf[CAP];
static wchar_t ext[CAP];

static HRESULT go(fnR f, int plen, size_t cch, int extbody){
    int i;
    for(i=0;i<CAP;++i) buf[i]=0x2A2A;
    for(i=0;i<plen;++i) buf[i]=L'a';
    buf[plen]=0;
    ext[0]=L'.';
    for(i=1;i<=extbody;++i) ext[i]=L'x';
    ext[extbody+1]=0;                       /* total extension length = extbody + 1 */
    return f(buf, cch, ext);
}

static void walk(fnR f, int plen, size_t cch){
    int e, prev=-1;
    printf("  path len %d, cch %llu:\n", plen, (unsigned long long)cch);
    for(e=0;e<=520;++e){
        HRESULT r = go(f,plen,cch,e);
        if((int)r != prev){
            printf("      extension length %-4d (body %-4d, result %-4d) -> %08lX\n",
                   e+1, e, plen+e+1, (unsigned long)r);
            prev=(int)r;
        }
    }
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    fnR f = h ? (fnR)GetProcAddress(h, "PathCchRenameExtension") : NULL;
    if(!f){ printf("cannot resolve PathCchRenameExtension\n"); return 2; }
    printf("Each line is a CHANGE of answer as the extension grows; the extension is \".xxx...\".\n"
           "The path has no dot, so the result length is (path len + extension len).\n\n");
    walk(f,   6, 1000);
    walk(f, 100, 1000);
    walk(f, 250, 1000);
    walk(f,   6,   20);
    printf("\nIf the boundary sits at the same EXTENSION length in every run it is a limit on the\n"
           "extension; if it moves with the path it is really the result length; if it moves with\n"
           "cch it is a buffer rule.\n");
    return 0;
}
