/* probes/which.c -- when BOTH size limits are breached, which code does PathCchRenameExtension pick?
 *
 * maxpath.c established that a result longer than 259 characters returns 0x800700CE
 * (ERROR_FILENAME_EXCED_RANGE) even when cch is generous, and that a result that merely will not
 * fit in cch returns the documented 0x8007007A (STRSAFE_E_INSUFFICIENT_BUFFER). It could not
 * separate the two, because every case it drew had cch-1 >= 259 whenever 259 was the binding
 * limit.
 *
 * This drives the three regions apart explicitly, with limit = min(cch-1, 259):
 *     A. result <= limit                         -> expect S_OK
 *     B. result >  259, cch-1 >= 259             -> 0x800700CE      (259 is binding)
 *     C. result >  cch-1, cch-1 < 259            -> 0x8007007A      (cch is binding)
 *     D. result >  both, cch-1 < 259             -> ??? THE QUESTION
 * and prints the resulting buffer each time, because the returned code is only half of what has to
 * be reproduced.
 */
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef HRESULT (WINAPI *fnR)(PWSTR, size_t, PCWSTR);

#define CAP 700
static wchar_t buf[CAP];
static wchar_t ext[400];

static void mkext(int n){ int i; ext[0]=L'.'; for(i=1;i<n;++i) ext[i]=L'x'; ext[n]=0; }

static void go(fnR f, const char* region, int len, size_t cch, int extlen){
    HRESULT r; int i,last=-1,term=-1;
    for(i=0;i<CAP;++i) buf[i]=0x2A2A;
    buf[0]=L'C'; buf[1]=L':'; buf[2]=L'\\';
    for(i=3;i<len;++i) buf[i]=L'a';
    buf[len]=0;
    mkext(extlen);
    r = f(buf, cch, ext);
    for(i=0;i<CAP;++i) if(buf[i]!=0x2A2A) last=i;
    for(i=0;i<CAP;++i) if(buf[i]==0){ term=i; break; }
    printf("  %-2s len=%-4d cch=%-5llu extlen=%-4d result=%-4d limit=%-4d -> %08lX  term@%-4d touched..%d\n",
           region, len, (unsigned long long)cch, extlen, len+extlen,
           (int)((cch-1) < 259 ? (cch-1) : 259), (unsigned long)r, term, last);
}

int main(void){
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    fnR f = h ? (fnR)GetProcAddress(h, "PathCchRenameExtension") : NULL;
    if(!f){ printf("cannot resolve PathCchRenameExtension\n"); return 2; }
    printf("limit = min(cch-1, 259); the path has no dot, so result = len + extlen\n\n");

    printf("A. fits everywhere\n");
    go(f,"A",  6,  20,  4);
    go(f,"A",250, 300,  4);
    go(f,"A",255, 300,  4);      /* result exactly 259 */

    printf("\nB. 259 is binding, cch is generous\n");
    go(f,"B",256, 300,  4);      /* result 260 */
    go(f,"B",256, 261,  4);      /* cch-1 = 260 >= 259 */
    go(f,"B",256, 260,  4);      /* cch-1 = 259, exactly the MAX_PATH limit */
    go(f,"B",  6, 300,300);      /* result 306 from a SHORT path and a long extension */

    printf("\nC. cch is binding, result stays within 259\n");
    go(f,"C",  6,   8,  4);      /* result 10, cch-1 = 7 */
    go(f,"C",250, 252,  4);      /* result 254, cch-1 = 251 */
    go(f,"C",200, 210, 50);      /* result 250, cch-1 = 209 */

    printf("\nD. BOTH breached and cch-1 < 259 -- the question\n");
    go(f,"D",256, 258,  4);      /* result 260, cch-1 = 257 */
    go(f,"D",250, 252, 20);      /* result 270, cch-1 = 251 */
    go(f,"D",  6,   8,300);      /* result 306, cch-1 = 7 */
    go(f,"D",100, 150,200);      /* result 300, cch-1 = 149 */
    go(f,"D",256, 259,  4);      /* result 260, cch-1 = 258 */

    printf("\nIf D answers 0x8007007A the smaller limit wins and 0x800700CE means only\n"
           "'cch was fine, 259 was not'. If D answers 0x800700CE the MAX_PATH breach wins\n"
           "outright. term@ is where the terminator ended up, which is what the buffer\n"
           "comparison in the live harness is actually checking.\n");
    return 0;
}
