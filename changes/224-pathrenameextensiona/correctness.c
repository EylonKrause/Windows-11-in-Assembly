// changes/224-pathrenameextensiona/correctness.c
// Gate 1: wia_pathrenameexta must be indistinguishable from shlwapi!PathRenameExtensionA.
// Three-way: our ASM vs the scalar oracle vs the LIVE export on this PC.
//
// THE WHOLE BUFFER IS COMPARED, always, and so is the BOOL. The export writes only the extension
// and its terminator and leaves everything past it stale ("file.txtxxxxxx" + ".o" ->
// "file.o\0txxxxxx"), and it must leave the buffer COMPLETELY untouched when the result does not
// fit -- neither of which a string comparison can see.
//
// AND THE CORPORA ENUMERATE RATHER THAN SAMPLE, WITH A SPACE IN THE ALPHABET. The wide sibling,
// change 158, shipped wrong for exactly the want of that: its corpus had no space in it, so its
// test, its oracle and its implementation shared one blind spot. Eight landed changes carried the
// same missing stopper.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern BOOL wia_pathrenameexta(char*, const char*);
BOOL ref_pathrenameexta(char*, const char*);
typedef BOOL (WINAPI *PRE)(char*, const char*);
static PRE sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<15) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON '#'
#define DSZ 640

static unsigned long sd = 0x77777u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd>>8; }

/* one three-way case: the BOOL and the whole buffer, ours vs oracle vs live */
static int chk(const char* path, const char* ext, const char* what)
{
    static char a[DSZ], b[DSZ], c[DSZ];
    memset(a, POISON, DSZ); memset(b, POISON, DSZ); memset(c, POISON, DSZ);
    size_t n = strlen(path);
    memcpy(a, path, n+1); memcpy(b, path, n+1); memcpy(c, path, n+1);
    BOOL ra = wia_pathrenameexta(a, ext);
    BOOL rb = ref_pathrenameexta(b, ext);
    BOOL rc = sys(c, ext);
    int ok = (!!ra == !!rb) && (!!ra == !!rc)
          && memcmp(a, b, DSZ) == 0 && memcmp(a, c, DSZ) == 0;
    if (!ok && fails < 15)
        printf("FAIL: %s -- path \"%s\" ext \"%s\": ours %d \"%s\" | oracle %d \"%s\" | live %d \"%s\"\n",
               what, path, ext, !!ra, a, !!rb, b, !!rc, c);
    if (!ok) ++fails;
    return ok;
}

int main(void){
    HMODULE h = LoadLibraryW(L"shlwapi.dll");
    sys = (PRE)GetProcAddress(h,"PathRenameExtensionA");
    if(!sys){ printf("CORRECTNESS: cannot resolve shlwapi!PathRenameExtensionA\n"); return 1; }
    printf("  GetACP() = %u\n", GetACP());

    static char s[640];

    // every probe-derived case explicitly
    {
        static const char* P[] = {
            "", ".", "..", "a", "a.", ".a", "a.b", "a.b.c", "file.txt", "file",
            "dir.x\\file", "dir.x\\file.txt", "dir\\", "\\", "\\.",
            "a.b/c", "a.b:c",                 /* '/' and ':' do NOT stop the search */
            "a.b c", "a.b  c", " .a", "a. b",  /* the SPACE does */
            "a.b\tc", "\t.a",                  /* a TAB does NOT -- the rule is 0x20 */
            "a b\\c.d", "a\\b c.d", "x .y", 0
        };
        static const char* E[] = { ".obj", "", ".", "obj", ". x", ".a\\b", ".a.b", ".zzzzzzzz", 0 };
        for (int i = 0; P[i]; ++i)
            for (int j = 0; E[j]; ++j)
                chk(P[i], E[j], "probe-derived case");
    }

    // EXHAUSTIVE over the alphabet that reaches every stopper candidate, INCLUDING THE SPACE.
    // 335923 strings, 238267 of them containing a space.
    {
        static const char AL[6] = { 'a', '.', '\\', '/', ':', ' ' };
        long en = 0, sp = 0;
        for (int n = 0; n <= 7; ++n) {
            long lim = 1; for (int i = 0; i < n; ++i) lim *= 6;
            for (long k = 0; k < lim; ++k) {
                long v = k; int has = 0;
                for (int i = 0; i < n; ++i) { s[i] = AL[v % 6]; if (s[i]==' ') has = 1; v /= 6; }
                s[n] = 0;
                chk(s, ".zz", "exhaustive {a,.,backslash,/,:,space}");
                ++en; sp += has;
            }
        }
        printf("  exhaustive {a,.,backslash,/,:,space} 0..7: %ld strings, %ld with a space\n", en, sp);
    }

    // the same alphabet against a RANGE of extension lengths, because the limit test is
    // pos + elen and only a varying elen exercises it
    {
        static const char AL[5] = { 'a', '.', '\\', ' ', ':' };
        static const char* E[] = { "", ".", ".o", ".obj", ".abcdefgh", 0 };
        for (int n = 0; n <= 5; ++n) {
            long lim = 1; for (int i = 0; i < n; ++i) lim *= 5;
            for (long k = 0; k < lim; ++k) {
                long v = k;
                for (int i = 0; i < n; ++i) { s[i] = AL[v % 5]; v /= 5; }
                s[n] = 0;
                for (int j = 0; E[j]; ++j) chk(s, E[j], "exhaustive x extension length");
            }
        }
    }

    // THE MAX_PATH BOUNDARY, SWEPT EXACTLY. The limit is on the RESULT, so every combination of
    // input length and extension length has to be walked across it -- an input-length-only sweep
    // would pass an implementation that bounded the wrong quantity.
    {
        static char big[640];
        for (int elen = 0; elen <= 8; ++elen) {
            char e[16];
            e[0] = '.';
            for (int i = 1; i <= elen; ++i) e[i] = 'o';
            e[elen+1] = 0;
            for (int len = 235; len <= 280; ++len) {
                for (int dot = -1; dot < 6; ++dot) {
                    for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
                    if (dot >= 0 && len - 1 - dot >= 0) big[len-1-dot] = '.';
                    big[len] = 0;
                    chk(big, e, "MAX_PATH boundary sweep");
                }
            }
        }
    }

    // long paths with a SPACE ahead of the extension -- the vector scan must carry the stopper
    // across 32-byte block boundaries, which no short corpus reaches
    {
        static char big[640];
        for (int len = 40; len <= 250; len += 7) {
            for (int sp = 1; sp < len - 6; sp += 11) {
                for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
                big[sp] = ' ';
                big[len-4] = '.';
                big[len] = 0;
                chk(big, ".obj", "long path, space before the extension");
                /* and with the dot BEFORE the space, so the space must suppress it */
                for (int i = 0; i < len; ++i) big[i] = (char)('a' + i % 23);
                big[sp] = '.';
                if (sp + 3 < len) big[sp+3] = ' ';
                big[len] = 0;
                chk(big, ".obj", "long path, space after the dot");
            }
        }
    }

    // EVERY byte value at the five positions the rule consults, in the path
    for (int c = 1; c < 256; ++c) {
        s[0]='a'; s[1]='b'; s[2]=(char)c; s[3]='c'; s[4]='d'; s[5]='.'; s[6]='t'; s[7]=0;
        chk(s, ".zz", "path byte sweep: a lone separator");
        s[0]='a'; s[1]='b'; s[2]='.'; s[3]='c'; s[4]=(char)c; s[5]='t'; s[6]=0;
        chk(s, ".zz", "path byte sweep: inside the extension");
        s[0]='a'; s[1]='b'; s[2]='.'; s[3]='c'; s[4]='d'; s[5]=(char)c; s[6]=0;
        chk(s, ".zz", "path byte sweep: the final byte");
        s[0]=(char)c; s[1]='a'; s[2]='b'; s[3]='.'; s[4]='t'; s[5]=0;
        chk(s, ".zz", "path byte sweep: the first byte");
        s[0]='a'; s[1]='b'; s[2]='.'; s[3]=(char)c; s[4]='t'; s[5]=0;
        chk(s, ".zz", "path byte sweep: just after the dot");
    }
    // EVERY byte value inside the EXTENSION -- it is copied verbatim, not validated
    for (int c = 1; c < 256; ++c) {
        char e[8];
        e[0]='.'; e[1]=(char)c; e[2]='z'; e[3]=0;
        chk("file.txt", e, "extension byte sweep");
        e[0]=(char)c; e[1]='z'; e[2]=0;
        chk("file.txt", e, "extension byte sweep, leading byte");
    }

    // 16 unaligned start offsets x lengths, so the page-check retry path is driven
    {
        static char buf[700];
        for (int offs = 0; offs < 16; ++offs) {
            char* p = buf + offs;
            for (int len = 0; len <= 70; ++len) {
                for (int i = 0; i < len; ++i) p[i] = (char)('a' + i % 23);
                if (len > 5) { p[len-4] = '.'; p[len/2] = ' '; }
                p[len] = 0;
                chk(p, ".obj", "unaligned start");
            }
        }
    }

    // NULLs
    {
        static char a[DSZ], b[DSZ], c[DSZ];
        memset(a, POISON, DSZ); memset(b, POISON, DSZ); memset(c, POISON, DSZ);
        memcpy(a, "file.txt", 9); memcpy(b, "file.txt", 9); memcpy(c, "file.txt", 9);
        BOOL ra = wia_pathrenameexta(a, 0);
        BOOL rb = ref_pathrenameexta(b, 0);
        BOOL rc = sys(c, 0);
        CHECK(!ra && !rb && !rc, "NULL extension returns FALSE");
        CHECK(memcmp(a,b,DSZ)==0 && memcmp(a,c,DSZ)==0, "NULL extension leaves the buffer alone");
        CHECK(!wia_pathrenameexta(0, ".obj"), "NULL path returns FALSE");
        CHECK(!ref_pathrenameexta(0, ".obj"), "NULL path returns FALSE (oracle)");
        CHECK(!sys(0, ".obj"), "NULL path returns FALSE (live)");
    }

    // randomized fuzz -- alphabet carries BOTH a space and a tab, because the rule is 0x20
    // specifically and not whitespace in general
    {
        static const char AL[10] = { 'a', 'b', '.', '\\', '/', ':', ' ', '\t', 'x', '.' };
        static const char* E[] = { "", ".", ".o", ".obj", ".a b", ".a\\b", ".a.b", ".abcdefghij", 0 };
        for (int t = 0; t < 300000; ++t) {
            int len = rnd() % 80;
            for (int i = 0; i < len; ++i) s[i] = AL[rnd() % 10];
            s[len] = 0;
            chk(s, E[rnd() % 8], "fuzz");
        }
    }

    // page guard on the PATH: the string ends exactly at a PAGE_NOACCESS boundary with no
    // extension, so the scan has to run to the very edge without reading over it
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        static char b[DSZ];
        for (int tail = 2; tail <= 90; ++tail) {
            /* the path ends at the guard; the rename must FAIL to fit, or write inside it.
               Use an empty extension so the result is never longer than the input. */
            char* p = (base+pg) - tail;
            for (int i = 0; i < tail-1; ++i) p[i] = (char)('a' + i % 23);
            p[tail-1] = 0;
            memset(b, POISON, DSZ);
            int n = 0; while (p[n]) { b[n] = p[n]; ++n; } b[n] = 0;
            BOOL ra = wia_pathrenameexta(p, "");     // must not read into page 2
            BOOL rb = ref_pathrenameexta(b, "");
            CHECK(!!ra == !!rb, "page-guard, no extension: BOOL");
            CHECK(memcmp(p, b, tail) == 0, "page-guard, no extension: buffer");
            if (tail >= 6) {
                for (int i = 0; i < tail-1; ++i) p[i] = (char)('a' + i % 23);
                p[tail-5] = '.';                     /* an extension right at the edge */
                p[tail-1] = 0;
                memset(b, POISON, DSZ);
                n = 0; while (p[n]) { b[n] = p[n]; ++n; } b[n] = 0;
                ra = wia_pathrenameexta(p, ".o");
                rb = ref_pathrenameexta(b, ".o");
                CHECK(!!ra == !!rb, "page-guard, extension at the edge: BOOL");
                CHECK(memcmp(p, b, tail) == 0, "page-guard, extension at the edge: buffer");
            }
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    // page guard on the EXTENSION: it has its own scan, so it needs its own guard
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base!=NULL,"VirtualAlloc (extension)");
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        for (int tail = 1; tail <= 70; ++tail) {
            char* e = (base+pg) - tail;
            e[0] = '.';
            for (int i = 1; i < tail-1; ++i) e[i] = 'o';
            e[tail-1] = 0;
            chk("C:\\dir\\file.txt", e, "page-guard on the extension");
        }
        VirtualFree(base,0,MEM_RELEASE);
    }

    if(fails){ printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathRenameExtensionA vs live shlwapi + oracle, BOOL and WHOLE-BUFFER "
           "compare incl. the stale tail: exhaustive {a,.,backslash,/,:,SPACE} to len 7 (335923 "
           "strings, 238267 with a space), the same alphabet x five extension lengths, the "
           "RESULT-length MAX_PATH boundary swept over input lengths 235..280 x extension lengths "
           "0..8 x six dot positions, long paths with a space on either side of the dot, ALL 255 "
           "byte values at five path positions and two extension positions, 16 unaligned starts, "
           "both NULLs, 300k fuzz carrying a space AND a tab, and NOACCESS page-guards on BOTH the "
           "path and the extension)\n");
    return 0;
}
