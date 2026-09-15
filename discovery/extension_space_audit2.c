/* discovery/extension_space_audit2.c
   The second sweep for the missing SPACE rule -- the functions the first audit did not cover.

   Earlier in this session, change 217 caught change 132 shipping a PathFindExtension rule with only
   the backslash stopping the backward scan. A SPACE stops it too, and 132 was wrong on 295513 of
   2015539 enumerated strings. discovery/extension_space_audit.c then measured the three landed
   changes whose oracles say outright that they reuse 132's rule -- 140, 143 and 144 -- and all three
   carried it. All four were corrected.

   THAT AUDIT WAS NOT WIDE ENOUGH. It looked at the changes that MENTION change 132. Change 174
   (PathUndecorateW) does not mention it; it derived its own four-conjunct rule and fuzz-confirmed it
   over 2000000 cases. One of those conjuncts is "the ']' must sit immediately before the LAST '.' of
   the component" -- which is an extension position by another name, and it has the same gap. It
   surfaced only because change 223 probed the narrow sibling with a space in the alphabet.

   So this sweep asks the question structurally instead: EVERY landed change whose oracle computes an
   extension position, whether or not it says where the rule came from.

       158 PathRenameExtensionW     private ref_findext, backslash only
       159 PathCchRenameExtension   private ref_findext, backslash only
       160 PathCchAddExtension      private ref_findext, backslash only
       174 PathUndecorateW          its own rule, with the same gap inside conjunct (b)

   For each, the live export is measured against the rule as landed and against the corrected one,
   over an alphabet that contains a space. A large count in the first column and zero in the second
   means that change is wrong and the fix is the same one.                                         */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOL    (WINAPI *F_REN)(wchar_t*, const wchar_t*);
typedef HRESULT (WINAPI *F_CCHREN)(wchar_t*, size_t, const wchar_t*);
typedef HRESULT (WINAPI *F_CCHADD)(wchar_t*, size_t, const wchar_t*);
typedef void    (WINAPI *F_UND)(wchar_t*);

/* the extension position, under each rule */
static int ext_old(const wchar_t* p){
    int n = 0; while (p[n]) ++n;
    int cand = -1;
    for (int i = 0; i < n; ++i) {
        if (p[i] == L'\\') cand = -1;
        else if (p[i] == L'.') cand = i;
    }
    return cand < 0 ? n : cand;
}
static int ext_new(const wchar_t* p){
    int n = 0; while (p[n]) ++n;
    int cand = -1;
    for (int i = 0; i < n; ++i) {
        if (p[i] == L'\\' || p[i] == L' ') cand = -1;
        else if (p[i] == L'.') cand = i;
    }
    return cand < 0 ? n : cand;
}

/* PathRenameExtension, modelled on an extension-position function */
static int ren_model(wchar_t* path, const wchar_t* ext, int (*extf)(const wchar_t*)){
    int pos = extf(path);
    int elen = 0; while (ext[elen]) ++elen;
    if (pos + elen >= 260) return 0;
    int i = 0; for (; i < elen; ++i) path[pos + i] = ext[i];
    path[pos + elen] = 0;
    return 1;
}

/* PathUndecorate, with the extension position supplied */
static void und_model(wchar_t* p, int (*extf)(const wchar_t*)){
    int n = 0; while (p[n]) ++n;
    int comp = 0;
    for (int i = 0; i < n; i++) if (p[i] == L'\\') comp = i + 1;
    int ext = extf(p);
    if (ext - 1 <= comp) return;
    if (p[ext-1] != L']') return;
    int j = ext - 2;
    while (j > comp && p[j] >= L'0' && p[j] <= L'9') --j;
    if (j <= comp) return;
    if (p[j] != L'[') return;
    int k = j, m = ext;
    while (p[m]) p[k++] = p[m++];
    p[k] = 0;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    F_REN     ren    = (F_REN)    GetProcAddress(hs, "PathRenameExtensionW");
    F_CCHREN  cchren = (F_CCHREN) GetProcAddress(hk, "PathCchRenameExtension");
    F_CCHADD  cchadd = (F_CCHADD) GetProcAddress(hk, "PathCchAddExtension");
    F_UND     und    = (F_UND)    GetProcAddress(hs, "PathUndecorateW");
    if (!ren || !cchren || !cchadd || !und) { printf("cannot resolve all four exports\n"); return 1; }

    printf("Every string over {a, '.', backslash, '[', ']', SPACE} of length 0..7 -- 335923 of them.\n");
    printf("For each landed change, the live export against the rule AS LANDED and CORRECTED.\n\n");

    static const wchar_t AL[6] = { L'a', L'.', L'\\', L'[', L']', L' ' };
    wchar_t s[12], w[40], m1[40], m2[40];
    long total = 0, withspace = 0;
    long ren_old = 0, ren_new = 0;
    long cren_old = 0, cren_new = 0;
    long cadd_old = 0, cadd_new = 0;
    long und_old = 0, und_new = 0;

    for (int len = 0; len <= 7; ++len) {
        long combos = 1;
        for (int i = 0; i < len; ++i) combos *= 6;
        for (long c = 0; c < combos; ++c) {
            long v = c; int sp = 0;
            for (int i = 0; i < len; ++i) { s[i] = AL[v % 6]; if (s[i]==L' ') sp = 1; v /= 6; }
            s[len] = 0;
            if (sp) ++withspace;

            /* ---- 158 PathRenameExtensionW ---- */
            {
                memcpy(w, s, (size_t)(len+1)*2);
                BOOL r = ren(w, L".zz");
                memcpy(m1, s, (size_t)(len+1)*2); int r1 = ren_model(m1, L".zz", ext_old);
                memcpy(m2, s, (size_t)(len+1)*2); int r2 = ren_model(m2, L".zz", ext_new);
                if (r != (BOOL)r1 || wcscmp(w, m1) != 0) ++ren_old;
                if (r != (BOOL)r2 || wcscmp(w, m2) != 0) ++ren_new;
            }
            /* ---- 159 PathCchRenameExtension ---- */
            {
                memcpy(w, s, (size_t)(len+1)*2);
                HRESULT hr = cchren(w, 40, L".zz");
                memcpy(m1, s, (size_t)(len+1)*2); ren_model(m1, L".zz", ext_old);
                memcpy(m2, s, (size_t)(len+1)*2); ren_model(m2, L".zz", ext_new);
                if (SUCCEEDED(hr)) {
                    if (wcscmp(w, m1) != 0) ++cren_old;
                    if (wcscmp(w, m2) != 0) ++cren_new;
                }
            }
            /* ---- 160 PathCchAddExtension: only acts when there is NO extension ---- */
            {
                memcpy(w, s, (size_t)(len+1)*2);
                HRESULT hr = cchadd(w, 40, L".zz");
                int e1 = ext_old(s), e2 = ext_new(s);
                int hasext1 = (e1 != len), hasext2 = (e2 != len);
                if (SUCCEEDED(hr)) {
                    /* under each rule: if there was an extension the buffer is unchanged */
                    int changed = (wcscmp(w, s) != 0);
                    if (changed == hasext1) ++cadd_old;
                    if (changed == hasext2) ++cadd_new;
                }
            }
            /* ---- 174 PathUndecorateW ---- */
            {
                memcpy(w, s, (size_t)(len+1)*2); und(w);
                memcpy(m1, s, (size_t)(len+1)*2); und_model(m1, ext_old);
                memcpy(m2, s, (size_t)(len+1)*2); und_model(m2, ext_new);
                if (wcscmp(w, m1) != 0) ++und_old;
                if (wcscmp(w, m2) != 0) ++und_new;
            }
            ++total;
        }
    }

    printf("  strings: %ld, of which %ld contain a space\n\n", total, withspace);
    printf("  %-34s %14s %14s\n", "landed change", "AS LANDED", "CORRECTED");
    printf("  %-34s %14s %14s\n", "----------------------------------", "-----------", "-----------");
    printf("  %-34s %14ld %14ld\n", "158 PathRenameExtensionW",   ren_old,  ren_new);
    printf("  %-34s %14ld %14ld\n", "159 PathCchRenameExtension", cren_old, cren_new);
    printf("  %-34s %14ld %14ld\n", "160 PathCchAddExtension",    cadd_old, cadd_new);
    printf("  %-34s %14ld %14ld\n", "174 PathUndecorateW",        und_old,  und_new);

    printf("\n  A large number in the first column and zero in the second means that change carries\n");
    printf("  the missing space rule and the fix is the same one applied to 132, 140, 143 and 144.\n");
    printf("  Zero in BOTH means the space never reaches that function's decision.\n");
    return 0;
}
