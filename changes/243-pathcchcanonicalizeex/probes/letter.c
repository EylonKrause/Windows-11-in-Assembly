/* changes/243-pathcchcanonicalizeex/probes/letter.c
   WHICH characters count as a drive letter? All 65536 code units, measured two ways.

   refcheck.c found the one assumption in the oracle that had never been tested: it treats only ASCII
   A-Z as a drive letter, and the 5.8 M enumerated cases were all ASCII. Live accepts U+00C5 (A with a
   ring) as a drive letter -- "\\?\{U+00C5}:\a\.." comes back as "{U+00C5}:\" with the extended prefix
   stripped -- and does NOT accept U+042F (Cyrillic YA). So the predicate is neither ASCII nor "any
   Unicode letter", and an implementation has to reproduce whatever it actually is.

   Two places consult it, and they are measured separately because nothing in this family may be
   inherited: the extended-prefix strip inside PathCchCanonicalizeEx, and PathCchIsRoot, which the
   canonicaliser calls on its own output.

   Each is compared against three candidate predicates -- ASCII A-Z, IsCharAlphaW, and GetStringTypeW's
   C1_ALPHA -- so the answer is a NAME rather than a table, if a name exists.

   Nothing here creates, opens or stats any file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000
typedef HRESULT (WINAPI *CANEX)(PWSTR, size_t, PCWSTR, ULONG);
typedef BOOL    (WINAPI *ISROOT)(PCWSTR);
static CANEX canex;
static ISROOT pcisroot;
static wchar_t out[4096];

static unsigned char accept_prefix[0x10000];   /* the drive-letter set used by the prefix strip */
static unsigned char accept_root[0x10000];     /* the drive-letter set used by PathCchIsRoot     */
static unsigned char is_alpha_api[0x10000];    /* IsCharAlphaW                                   */
static unsigned char is_alpha_ct1[0x10000];    /* GetStringTypeW C1_ALPHA                        */

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    canex   = (CANEX)GetProcAddress(hk, "PathCchCanonicalizeEx");
    pcisroot = (ISROOT)GetProcAddress(hk, "PathCchIsRoot");
    if (!canex || !pcisroot) { printf("cannot resolve the exports\n"); return 1; }

    /* --- the prefix strip: "\\?\X:\a" keeps the prefix unless X is a drive letter --------- */
    for (unsigned c = 1; c < 0x10000; ++c) {
        wchar_t in[16];
        in[0]=L'\\'; in[1]=L'\\'; in[2]=L'?'; in[3]=L'\\';
        in[4]=(wchar_t)c; in[5]=L':'; in[6]=L'\\'; in[7]=L'a'; in[8]=0;
        out[0]=0;
        if (canex(out, PATHCCH_MAX_CCH, in, 0) == S_OK)
            accept_prefix[c] = (out[0] == (wchar_t)c) ? 1 : 0;   /* stripped => a drive letter */
    }
    /* --- PathCchIsRoot("X:\") ------------------------------------------------------------- */
    for (unsigned c = 1; c < 0x10000; ++c) {
        wchar_t in[8];
        in[0]=(wchar_t)c; in[1]=L':'; in[2]=L'\\'; in[3]=0;
        accept_root[c] = pcisroot(in) ? 1 : 0;
    }
    /* --- the two candidate predicates ----------------------------------------------------- */
    for (unsigned c = 1; c < 0x10000; ++c) {
        wchar_t w = (wchar_t)c;
        WORD t = 0;
        is_alpha_api[c] = IsCharAlphaW(w) ? 1 : 0;
        if (GetStringTypeW(CT_CTYPE1, &w, 1, &t)) is_alpha_ct1[c] = (t & C1_ALPHA) ? 1 : 0;
    }

    /* --- compare ------------------------------------------------------------------------- */
    {
        long long n_pref = 0, n_root = 0, n_api = 0, n_ct1 = 0;
        long long d_pr = 0, d_pa = 0, d_pc = 0, d_ra = 0;
        long long ascii_only_pref = 0;
        for (unsigned c = 1; c < 0x10000; ++c) {
            int ascii = ((c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z'));
            n_pref += accept_prefix[c]; n_root += accept_root[c];
            n_api  += is_alpha_api[c];  n_ct1  += is_alpha_ct1[c];
            if (accept_prefix[c] != accept_root[c])    ++d_pr;
            if (accept_prefix[c] != is_alpha_api[c])   ++d_pa;
            if (accept_prefix[c] != is_alpha_ct1[c])   ++d_pc;
            if (accept_root[c]   != is_alpha_api[c])   ++d_ra;
            if (accept_prefix[c] != ascii)             ++ascii_only_pref;
        }
        printf("accepted as a drive letter by the prefix strip : %lld of 65535\n", n_pref);
        printf("accepted as a drive letter by PathCchIsRoot    : %lld\n", n_root);
        printf("IsCharAlphaW                                   : %lld\n", n_api);
        printf("GetStringTypeW C1_ALPHA                        : %lld\n", n_ct1);
        printf("\ndifferences\n");
        printf("  prefix vs PathCchIsRoot   : %lld\n", d_pr);
        printf("  prefix vs IsCharAlphaW    : %lld\n", d_pa);
        printf("  prefix vs C1_ALPHA        : %lld\n", d_pc);
        printf("  root   vs IsCharAlphaW    : %lld\n", d_ra);
        printf("  prefix vs ASCII A-Za-z    : %lld\n", ascii_only_pref);
    }

    printf("\n=== the accepted set outside ASCII, first 80 ===\n   ");
    { int k = 0;
      for (unsigned c = 0x80; c < 0x10000 && k < 80; ++c)
          if (accept_prefix[c]) { printf(" %04X", c); if (++k % 16 == 0) printf("\n   "); }
      printf("\n"); }

    printf("\n=== ASCII characters accepted (should be A-Z a-z) ===\n   ");
    for (unsigned c = 1; c < 0x80; ++c) if (accept_prefix[c]) printf(" %04X", c);
    printf("\n");

    printf("\n=== where the prefix predicate and IsCharAlphaW disagree, first 40 ===\n");
    { int k = 0;
      for (unsigned c = 1; c < 0x10000 && k < 40; ++c)
          if (accept_prefix[c] != is_alpha_api[c]) {
              printf("    U+%04X prefix %d IsCharAlphaW %d C1_ALPHA %d\n",
                     c, accept_prefix[c], is_alpha_api[c], is_alpha_ct1[c]);
              ++k;
          }
      if (!k) printf("    none\n"); }

    printf("\n=== the accepted set as ranges ===\n");
    { unsigned start = 0; int in_run = 0, runs = 0;
      for (unsigned c = 1; c <= 0x10000; ++c) {
          int a = (c < 0x10000) ? accept_prefix[c] : 0;
          if (a && !in_run) { start = c; in_run = 1; }
          else if (!a && in_run) {
              printf("    %04X..%04X (%u)\n", start, c-1, c-start);
              in_run = 0;
              if (++runs >= 40) { printf("    ...\n"); break; }
          }
      } }
    return 0;
}
