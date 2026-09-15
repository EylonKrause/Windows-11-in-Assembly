/* changes/243-pathcchcanonicalizeex/probes/algebra.c
   The complete separator-and-dot algebra of PathCchCanonicalizeEx, live beside the candidate model.

   model.c's candidate matched 99.63% of 797161 enumerated paths and every residual mismatch had live
   returning "\\" where the model returned "\". Every one of those inputs is separators and dots only,
   so this prints that ENTIRE subspace to length 7 -- 255 strings, small enough to read as a table --
   with the model's answer next to the live one. Reading the whole closed subspace at once is how the
   rule gets found; guessing one case at a time is how change 236 wasted four rounds.

   Nothing here creates, opens or stats any file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000
typedef HRESULT (WINAPI *CANEX)(PWSTR, size_t, PCWSTR, ULONG);
static CANEX canex;

/* ---- the candidate model, as documented in model.c ------------------------------------------- */
#define MAXCOMP 1024
static int is_letter(wchar_t c){ return (c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z'); }
static int ieq(const wchar_t* a, const wchar_t* b, size_t n){
    for (size_t i=0;i<n;++i){ wchar_t x=a[i],y=b[i];
        if(x>=L'a'&&x<=L'z')x-=32; if(y>=L'a'&&y<=L'z')y-=32; if(x!=y) return 0; }
    return 1;
}
static int model(wchar_t* o, size_t occh, const wchar_t* in)
{
    wchar_t w[4096];
    size_t n, rootlen, ol;
    size_t prelen[MAXCOMP], cstart[MAXCOMP];
    int ncomp = 0, needsep = 0;
    {
        size_t len = wcslen(in);
        if (len + 8 >= 4096 || len + 8 >= occh) return 1;
        if (len >= 4 && in[0]==L'\\' && in[1]==L'\\' && in[2]==L'?' && in[3]==L'\\') {
            const wchar_t* r = in + 4; size_t rl = len - 4;
            if (rl >= 2 && is_letter(r[0]) && r[1]==L':' && (rl==2 || r[2]==L'\\')) wcscpy(w, r);
            else if (rl >= 4 && ieq(r, L"UNC\\", 4)) { w[0]=L'\\'; w[1]=L'\\'; wcscpy(w+2, r+4); }
            else wcscpy(w, in);
        } else wcscpy(w, in);
        n = wcslen(w);
    }
    if (n >= 3 && is_letter(w[0]) && w[1]==L':' && w[2]==L'\\') rootlen = 3;
    else if (n >= 2 && w[0]==L'\\' && w[1]==L'\\')              rootlen = 2;
    else if (n >= 1 && w[0]==L'\\')                            rootlen = 1;
    else                                                       rootlen = 0;
    memcpy(o, w, rootlen*sizeof(wchar_t));
    ol = rootlen;
    {
        size_t i = rootlen;
        for (;;) {
            size_t s = i;
            while (i < n && w[i] != L'\\') ++i;
            size_t clen = i - s;
            if (clen==1 && w[s]==L'.') { }
            else if (clen==2 && w[s]==L'.' && w[s+1]==L'.') {
                if (ncomp > 0) { ol = prelen[--ncomp]; needsep = 1; }
                else { ol = rootlen; needsep = 0; }
            } else {
                size_t pl;
                if (ncomp >= MAXCOMP) return 1;
                if (ncomp == 0 && !needsep) pl = rootlen ? rootlen - 1 : 0;
                else { pl = ol; if (ol+1 >= occh) return 1; o[ol++] = L'\\'; }
                if (ol + clen >= occh) return 1;
                prelen[ncomp] = pl; cstart[ncomp] = ol; ++ncomp;
                memcpy(o+ol, w+s, clen*sizeof(wchar_t)); ol += clen;
                needsep = 0;
            }
            if (i >= n) break;
            ++i;
        }
    }
    if (ncomp > 0) { size_t lo = cstart[ncomp-1]; while (ol > lo && o[ol-1]==L'.') --ol; }
    o[ol] = 0;
    if (ol == 0) { if (occh < 2) return 1; o[0]=L'\\'; o[1]=0; }
    else if (ol == 2 && o[1] == L':') { if (ol+2 > occh) return 1; o[ol]=L'\\'; o[ol+1]=0; }
    return 0;
}

/* ---- the table --------------------------------------------------------------------------------- */
static wchar_t live[4096], mine[4096];

static void row(const wchar_t* in)
{
    for (int i = 0; i < 64; ++i) live[i] = 0xCDCD;
    HRESULT hr = canex(live, PATHCCH_MAX_CCH, in, 0);
    if (hr != S_OK) { printf("   %-9ls -> %08lX\n", in, (unsigned long)hr); return; }
    if (model(mine, 4096, in)) { printf("   %-9ls -> live %-9ls model DECLINED\n", in, live); return; }
    int bad = wcscmp(mine, live) != 0;
    printf("   %-9ls -> %-9ls %s%ls\n", in, live, bad ? "MODEL " : "", bad ? mine : L"");
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    canex = (CANEX)GetProcAddress(hk, "PathCchCanonicalizeEx");
    if (!canex) { printf("cannot resolve PathCchCanonicalizeEx\n"); return 1; }

    printf("=== every string over { \\ . } to length 7, live -> then the model when it differs ===\n");
    {
        wchar_t buf[16];
        static const wchar_t* A = L"\\.";
        for (int len = 0; len <= 7; ++len) {
            long long total = 1; for (int i = 0; i < len; ++i) total *= 2;
            printf("  -- length %d\n", len);
            for (long long v = 0; v < total; ++v) {
                long long x = v;
                for (int i = 0; i < len; ++i) { buf[i] = A[x & 1]; x >>= 1; }
                buf[len] = 0;
                row(buf);
            }
        }
    }
    return 0;
}
