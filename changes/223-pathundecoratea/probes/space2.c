/* changes/223-pathundecoratea/probes/space2.c
   PathUndecorate's extension search stops at a SPACE -- and does change 174 know?

   probes/undec.c enumerated the narrow PathUndecorateA against the rule change 174 derived for the
   wide form. Over {a, '[', ']', '.', backslash, '0'} the two agree on all 335923 strings. Add a
   SPACE to the alphabet and 2724 of them disagree, the smallest being:

       ". []"   live -> ". "        174's rule -> ". []"   (unchanged)

   Reading that case gives the answer. 174's rule looks for the component's last '.', finds the one
   at index 0, and concludes there is no room for a group before it. The live export instead behaves
   as though the component has NO extension at all -- which is exactly what PathFindExtension says
   once its SPACE stopper is taken into account, the rule this session had to correct in changes 132,
   140, 143 and 144.

   So the amendment under test is: the EXTENSION position is the one PathFindExtension computes --
   the last '.' after the last backslash OR SPACE -- while the COMPONENT used by conjunct (d), that
   the '[' may not be the component's first character, stays delimited by backslash alone.

   AND THE SAME QUESTION IS PUT TO THE WIDE FORM. Change 174 is landed. Its contract was
   "fuzz-confirmed against the live export, 2000000 cases, 0 mismatches" -- and if that corpus had no
   space in it, 174 carries the identical gap, exactly as 140, 143 and 144 carried 132's.          */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef void (WINAPI *FA)(char*);
typedef void (WINAPI *FW)(wchar_t*);
static FA ua;
static FW uw;

/* ---- the rule change 174 derived, transcribed to bytes ---- */
static void model_174(char* p){
    int n = 0; while (p[n]) ++n;
    int comp = 0;
    for (int i = 0; i < n; i++) if (p[i] == '\\') comp = i + 1;
    int ext = n;
    for (int i = n - 1; i >= comp; --i) if (p[i] == '.') { ext = i; break; }
    if (ext - 1 <= comp) return;
    if (p[ext-1] != ']') return;
    int j = ext - 2;
    while (j > comp && p[j] >= '0' && p[j] <= '9') --j;
    if (j <= comp) return;
    if (p[j] != '[') return;
    int k = j, m = ext;
    while (p[m]) p[k++] = p[m++];
    p[k] = 0;
}

/* ---- the amendment: the extension search stops at a SPACE as well as a backslash ---- */
static void model_fixed(char* p){
    int n = 0; while (p[n]) ++n;
    int comp = 0;
    for (int i = 0; i < n; i++) if (p[i] == '\\') comp = i + 1;
    /* PathFindExtension's rule: scan back from the end, stop at a backslash OR A SPACE */
    int ext = n;
    for (int q = n; q > 0; ) {
        --q;
        if (p[q] == '.')  { ext = q; break; }
        if (p[q] == '\\' || p[q] == ' ') break;
    }
    if (ext - 1 <= comp) return;
    if (p[ext-1] != ']') return;
    int j = ext - 2;
    while (j > comp && p[j] >= '0' && p[j] <= '9') --j;
    if (j <= comp) return;
    if (p[j] != '[') return;
    int k = j, m = ext;
    while (p[m]) p[k++] = p[m++];
    p[k] = 0;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    ua = (FA)GetProcAddress(hs, "PathUndecorateA");
    uw = (FW)GetProcAddress(hs, "PathUndecorateW");
    if (!ua || !uw) { printf("cannot resolve both exports\n"); return 1; }
    printf("GetACP() = %u\n\n", GetACP());

    printf("=== the smallest failing case, in detail ===\n");
    {
        char a[16], b[16], c[16];
        strcpy(a, ". []"); strcpy(b, ". []"); strcpy(c, ". []");
        ua(a); model_174(b); model_fixed(c);
        printf("  \". []\"  live A = \"%s\"   174's rule = \"%s\"   amended = \"%s\"\n", a, b, c);
        {
            wchar_t w[16];
            const char* src = ". []";
            int i = 0;
            for (; src[i]; ++i) w[i] = (wchar_t)src[i];
            w[i] = 0;
            uw(w);
            printf("  and the WIDE export gives \"");
            for (i = 0; w[i]; ++i) putchar((char)w[i]);
            printf("\"\n");
        }
    }

    printf("\n=== exhaustive: {'[', ']', '.', '0', space, 'z'}, lengths 0..7, BOTH exports ===\n");
    {
        static const char AL[6] = { '[', ']', '.', '0', ' ', 'z' };
        char s[12], a[24], b[24], c[24], wn[24];
        wchar_t w[24];
        long total = 0, aw = 0, a174 = 0, w174 = 0, afix = 0, wfix = 0, withspace = 0;
        int shown = 0;
        for (int len = 0; len <= 7; ++len) {
            long combos = 1;
            for (int i = 0; i < len; ++i) combos *= 6;
            for (long cc = 0; cc < combos; ++cc) {
                long v = cc; int sp = 0;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 6]; if (s[i]==' ') sp = 1; v /= 6; }
                s[len] = 0;
                if (sp) ++withspace;

                memcpy(a, s, (size_t)len + 1); ua(a);
                for (int i = 0; i <= len; ++i) w[i] = (wchar_t)(unsigned char)s[i];
                uw(w);
                { int i = 0; for (; w[i]; ++i) wn[i] = (char)w[i]; wn[i] = 0; }

                memcpy(b, s, (size_t)len + 1); model_174(b);
                memcpy(c, s, (size_t)len + 1); model_fixed(c);

                if (strcmp(a, wn) != 0) { if (++aw <= 4) printf("  A/W DIFFER on \"%s\"\n", s); }
                if (strcmp(a, b) != 0) ++a174;
                if (strcmp(wn, b) != 0) ++w174;
                if (strcmp(a, c) != 0) {
                    if (shown < 6) { printf("  amended MISMATCH \"%s\": live \"%s\" amended \"%s\"\n", s, a, c); ++shown; }
                    ++afix;
                }
                if (strcmp(wn, c) != 0) ++wfix;
                ++total;
            }
        }
        printf("\n  strings tested                          : %ld (%ld containing a space)\n", total, withspace);
        printf("  A and W disagree with EACH OTHER on     : %ld\n", aw);
        printf("  A vs change 174's LANDED rule           : %ld mismatches\n", a174);
        printf("  W vs change 174's LANDED rule           : %ld mismatches\n", w174);
        printf("  A vs the amended rule (space stops ext) : %ld mismatches\n", afix);
        printf("  W vs the amended rule (space stops ext) : %ld mismatches\n", wfix);
        printf("\n  => %s\n", (afix == 0 && wfix == 0)
               ? "THE AMENDMENT IS THE RULE, for BOTH exports. Change 174 must be corrected."
               : "the amendment is still incomplete -- keep digging");
    }
    return 0;
}
