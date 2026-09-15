/* changes/223-pathundecoratea/probes/bytes.c
   The byte-wise screen at EVERY structural position, not just inside the group.

   probes/undec.c varied all 256 byte values INSIDE the bracketed group, where the digit test
   happens, and found 0 of 255 disagreeing with a byte-wise model. That is the position the rule
   talks about most, but it is not the only position the function looks at, and a narrow export can
   be MBCS-aware at any of them: a lead byte sitting immediately before a '[' or a '.' would fuse
   with it into one character and change what the scan sees, without touching the digit test at all.

   The active code page here is 1252, which HAS NO DBCS LEAD BYTES -- established earlier in this
   repository and re-printed below -- so a byte-wise implementation is safe on this system by
   construction. That is a fact about the machine, though, not about the function, and the whole
   reason this repository probes narrow siblings separately is that "the names match" is not
   evidence. StrStrA survived three reproductions of its fold and died on the fourth.

   So: for each of the five positions the rule actually consults, put every one of the 256 byte
   values there and compare the live export against a purely byte-wise model.

       1. immediately BEFORE the '['            (the component character that must not be a boundary)
       2. immediately AFTER the ']'             (must be '.' or the terminator for the group to go)
       3. immediately AFTER the '.'             (the first character of the extension)
       4. as the component's FIRST character    (conjunct (d) -- '[' may not start the component)
       5. as a lone separator before the name   (does any byte other than backslash start a
                                                 component? and does any byte other than SPACE stop
                                                 the extension search?)

   Position 5 is the one that matters most after this session: it re-derives the stopper set from
   scratch rather than inheriting "backslash and space" from change 174's correction.             */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef void (WINAPI *FN)(char*);
static FN und;

#define POISON '#'
#define NBUF 64

/* the corrected rule, byte-wise: the extension search stops at a backslash OR A SPACE, while the
   component used by conjunct (d) is delimited by the backslash alone */
static void model(char* p){
    int n = 0; while (p[n]) ++n;
    int comp = 0;
    for (int i = 0; i < n; i++) if (p[i] == '\\') comp = i + 1;
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

/* run one template with byte value v substituted at the '?' and report whether live == model,
   comparing the WHOLE buffer because this function leaves a stale tail past the terminator */
static int one(const char* tmpl, int v, int* undecorated){
    char a[NBUF], b[NBUF];
    int n = (int)strlen(tmpl), i;
    memset(a, POISON, NBUF); memset(b, POISON, NBUF);
    for (i = 0; i < n; ++i) { char c = tmpl[i] == '?' ? (char)v : tmpl[i]; a[i] = c; b[i] = c; }
    a[n] = 0; b[n] = 0;
    und(a);
    model(b);
    if ((int)strlen(a) != n) ++*undecorated;
    return memcmp(a, b, NBUF) != 0;
}

static void sweep(const char* tmpl, const char* what){
    int bad = 0, undecorated = 0, shown = 0, v;
    for (v = 0; v < 256; ++v) {
        if (v == 0) continue;                       /* a NUL would just end the string early */
        if (one(tmpl, v, &undecorated)) {
            ++bad;
            if (shown < 4) {
                char a[NBUF], b[NBUF];
                int n = (int)strlen(tmpl), i;
                memset(a, POISON, NBUF); memset(b, POISON, NBUF);
                for (i = 0; i < n; ++i) { char c = tmpl[i]=='?' ? (char)v : tmpl[i]; a[i]=c; b[i]=c; }
                a[n] = 0; b[n] = 0;
                und(a); model(b);
                printf("      byte 0x%02X: live \"%s\"  model \"%s\"\n", v, a, b);
                ++shown;
            }
        }
    }
    printf("  %-46s %-18s %3d of 255 disagree   (%d undecorated)\n", tmpl, what, bad, undecorated);
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    und = (FN)GetProcAddress(hs, "PathUndecorateA");
    if (!und) { printf("cannot resolve PathUndecorateA\n"); return 1; }

    printf("GetACP() = %u\n", GetACP());
    {
        CPINFO ci;
        int lead = 0, i;
        if (GetCPInfo(CP_ACP, &ci)) {
            for (i = 0; i < MAX_LEADBYTES && ci.LeadByte[i]; i += 2) lead += ci.LeadByte[i+1] - ci.LeadByte[i] + 1;
        }
        printf("DBCS lead bytes in the active code page: %d  -- %s\n\n", lead,
               lead ? "a byte-wise implementation would NOT be safe here"
                    : "so a byte-wise implementation is safe on this system");
    }

    printf("=== every byte value at each position the rule consults ===\n");
    printf("  (whole-buffer compare against a byte-wise model of the CORRECTED rule)\n\n");
    sweep("file?[12].txt", "before the '['");
    sweep("file[12]?.txt", "after the ']'");
    sweep("file[12].?txt", "after the '.'");
    sweep("?[12].txt",     "component start");
    sweep("ab?file[12].txt", "a lone separator");
    sweep("ab?file[12]",   "separator, no dot");

    printf("\n=== which bytes STOP the extension search? ===\n");
    printf("  \"a.b?[1].c\" undecorates only if the byte does NOT stop the search before the\n");
    printf("  last dot. Anything that reports a difference from the backslash-only model is a\n");
    printf("  stopper; the corrected rule says exactly backslash and space.\n");
    {
        int v, found = 0;
        for (v = 1; v < 256; ++v) {
            char a[NBUF];
            const char* t = "x?y[1]";                /* no dot: the group hugs the end */
            int n = (int)strlen(t), i;
            memset(a, POISON, NBUF);
            for (i = 0; i < n; ++i) a[i] = t[i]=='?' ? (char)v : t[i];
            a[n] = 0;
            und(a);
            /* the group goes unless the byte started a new component, i.e. it is a backslash */
            if ((int)strlen(a) == n) {
                printf("    0x%02X blocked the removal", v);
                if (v == '\\') printf("   (the backslash: starts a new component)");
                printf("\n");
                ++found;
            }
        }
        if (!found) printf("    none\n");
    }
    {
        int v;
        printf("\n  and with a dot present -- \"x?y.z[1].e\" style, which asks whether the byte\n");
        printf("  stops the EXTENSION search rather than starting a component:\n");
        for (v = 1; v < 256; ++v) {
            char a[NBUF], b[NBUF];
            const char* t = "ab?cd[1].e";
            int n = (int)strlen(t), i;
            memset(a, POISON, NBUF); memset(b, POISON, NBUF);
            for (i = 0; i < n; ++i) { char c = t[i]=='?' ? (char)v : t[i]; a[i]=c; b[i]=c; }
            a[n] = 0; b[n] = 0;
            und(a); model(b);
            if (memcmp(a, b, NBUF) != 0) printf("    0x%02X disagrees with the corrected rule\n", v);
        }
        printf("    (nothing listed above means the corrected rule holds for all 255)\n");
    }
    return 0;
}
