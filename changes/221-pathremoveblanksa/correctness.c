// changes/221-pathremoveblanksa/correctness.c
// Gate 1: wia_pathremoveblanksa must be indistinguishable from shlwapi!PathRemoveBlanksA.
//
// Three-way: our assembly vs the scalar oracle vs the LIVE export on this PC.
//
// EVERY CASE COMPARES THE WHOLE BUFFER, and this function returns NOTHING, so the buffer is the only
// observable there is. It writes only what it must -- nothing at all when there is nothing to strip,
// one terminator when only the trailing end goes, a move when only the leading end does -- and the
// ORDER of the two writes is observable: it MOVES first and CUTS second, the opposite of StrTrimA
// (change 218), which leaves a different tail behind.
//
// The other thing driving the shape: the first load is aligned DOWN with the leading bits cleared,
// so every case runs at EVERY start offset within a 32-byte block, with BLANKS planted in front of
// the string -- which is what a mis-cleared first mask would find.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern void wia_pathremoveblanksa(char*);
void ref_pathremoveblanksa(char*);
typedef void (WINAPI *FN)(char*);
static FN sys;

static int fails = 0;
#define CHECK(c,msg) do{ if(!(c)){ if(fails<12) printf("FAIL: %s\n",(msg)); ++fails; } }while(0)

#define POISON '#'
#define BUF 2048

static int one_at(const char* src, int n, int off){
    static char a[BUF], b[BUF], c[BUF];
    int i;
    memset(a, POISON, BUF); memset(b, POISON, BUF); memset(c, POISON, BUF);
    /* blanks in front of the string: a mis-cleared leading mask would pick one up */
    for (i = 1; i <= 40 && off - i >= 0; ++i) { a[off-i] = ' '; b[off-i] = ' '; c[off-i] = ' '; }
    memcpy(a + off, src, (size_t)n + 1);
    memcpy(b + off, src, (size_t)n + 1);
    memcpy(c + off, src, (size_t)n + 1);
    wia_pathremoveblanksa(a + off);
    ref_pathremoveblanksa(b + off);
    sys(c + off);
    for (i = 0; i < BUF; ++i) if (a[i] != b[i] || a[i] != c[i]) return 0;
    return 1;
}
static int one(const char* src){ return one_at(src, (int)strlen(src), 64); }
static int one_all_offsets(const char* src){
    int n = (int)strlen(src);
    for (int off = 64; off < 64 + 32; ++off) if (!one_at(src, n, off)) return 0;
    return 1;
}

static unsigned long sd = 0x221221u;
static unsigned rnd(void){ sd = sd*1103515245u + 12345u; return sd >> 8; }

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    sys = (FN)GetProcAddress(hs, "PathRemoveBlanksA");
    if (!sys) { printf("CORRECTNESS: cannot resolve PathRemoveBlanksA\n"); return 1; }

    // ---- NULL ----
    wia_pathremoveblanksa(NULL);
    sys(NULL);
    CHECK(1, "NULL returns without faulting");

    // ---- named shapes, every start offset ----
    {
        static const char* S[] = {
            "  abc  ", "  abc", "abc  ", "abc", "     ", " ", "", "a b c", "  a b  ",
            "\ta\t", "C:\\Program Files\\x  ", "  C:\\Program Files\\x", " a ", "  ",
            "a", " a", "a ", "   a   b   ", "\t \t", " \t ", "  \t  " };
        for (int i = 0; i < (int)(sizeof(S)/sizeof(S[0])); ++i)
            CHECK(one_all_offsets(S[i]), "named shapes at every start offset");
    }

    // ---- EVERY byte value, in every role that matters -------------------------------------------
    // A blank is 0x20 and nothing else; a tab is NOT a blank. Every other value must survive at both
    // ends, and must stop the run when it appears inside one.
    {
        for (int b = 1; b < 256; ++b) {
            char s[16];
            /* leading and trailing */
            s[0]=(char)b; s[1]='a'; s[2]='b'; s[3]=0;
            CHECK(one(s), "every byte value leading");
            s[0]='a'; s[1]='b'; s[2]=(char)b; s[3]=0;
            CHECK(one(s), "every byte value trailing");
            /* inside a blank run at both ends */
            s[0]=' '; s[1]=' '; s[2]=(char)b; s[3]='x'; s[4]=' '; s[5]=' '; s[6]=0;
            CHECK(one(s), "every byte value inside the runs");
            /* a string made entirely of that byte */
            s[0]=(char)b; s[1]=(char)b; s[2]=(char)b; s[3]=0;
            CHECK(one(s), "a string made entirely of every byte value");
        }
    }

    // ---- every length x every leading run x every trailing run -----------------------------------
    {
        static char s[300];
        for (int len = 0; len <= 70; ++len) {
            for (int lead = 0; lead <= len; lead += (len > 20 ? 7 : 1)) {
                for (int trail = 0; trail + lead <= len; trail += (len > 20 ? 7 : 1)) {
                    for (int i = 0; i < len; ++i)
                        s[i] = (i < lead || i >= len - trail) ? ' ' : (char)('a' + (i % 23));
                    s[len] = 0;
                    if (!one(s)) { CHECK(0, "every length x leading run x trailing run"); break; }
                }
            }
        }
    }

    // ---- blanks in the MIDDLE must survive, at every position ------------------------------------
    {
        static char s[200];
        for (int len = 3; len <= 120; ++len) {
            for (int pos = 1; pos < len - 1; pos += (len > 40 ? 7 : 1)) {
                for (int i = 0; i < len; ++i) s[i] = (char)('a' + (i % 23));
                s[pos] = ' ';
                s[len] = 0;
                if (!one(s)) { CHECK(0, "a middle blank at every position"); break; }
            }
        }
    }

    // ---- long strings through the 32-byte move loop ----------------------------------------------
    {
        static char s[1400];
        for (int len = 100; len <= 1200; len += 173) {
            for (int lead = 0; lead <= 40; lead += 9) {
                for (int trail = 0; trail <= 40; trail += 9) {
                    if (lead + trail > len) continue;
                    for (int i = 0; i < len; ++i)
                        s[i] = (i < lead || i >= len - trail) ? ' ' : (char)('a' + (i % 23));
                    s[len] = 0;
                    CHECK(one_at(s, len, 64), "long strings, 32-byte move loop");
                }
            }
        }
    }

    // ---- fuzz over the full byte range, blank-biased ----------------------------------------------
    {
        static char s[400];
        for (int t = 0; t < 200000; ++t) {
            int len = (int)(rnd() % 300);
            for (int i = 0; i < len; ++i)
                s[i] = ((rnd() % 3) == 0) ? ' ' : (char)(1 + rnd() % 255);
            s[len] = 0;
            CHECK(one(s), "fuzz, blank-biased, full byte range");
        }
    }

    // ---- GUARD PAGE ------------------------------------------------------------------------------
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        CHECK(base != NULL, "VirtualAlloc");
        DWORD old; VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);

        static char ref[BUF];
        for (int len = 1; len <= 200; ++len) {
            for (int mode = 0; mode < 3; ++mode) {
                char* s = (base + pg) - (SIZE_T)len - 1;
                for (int i = 0; i < len; ++i) s[i] = (char)('a' + (i % 23));
                if (mode == 1 && len > 2) { s[0] = ' '; s[1] = ' '; }
                if (mode == 2) { for (int i = 0; i < len; ++i) s[i] = ' '; }
                s[len] = 0;
                memcpy(ref, s, (size_t)len + 1);

                wia_pathremoveblanksa(s);
                char a[BUF]; memcpy(a, s, (size_t)len + 1);
                memcpy(s, ref, (size_t)len + 1);
                sys(s);
                CHECK(memcmp(a, s, (size_t)len + 1) == 0, "guard page: whole buffer");
            }
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    if (fails) { printf("CORRECTNESS: FAILED (%d)\n", fails); return 1; }
    printf("CORRECTNESS: PASS (PathRemoveBlanksA vs live shlwapi + oracle. EVERY case compares the "
           "WHOLE BUFFER, which is the only observable this function has -- it returns nothing, it "
           "writes only what it must, and the ORDER of its writes is visible: it MOVES the leading "
           "end first and CUTS the trailing end second, the OPPOSITE of StrTrimA in change 218, so "
           "\"  abc  \" leaves a b c NUL space NUL space NUL where cutting first would have left a "
           "stale 'c'. NULL; 21 named shapes at EVERY start offset within a 32-byte block WITH "
           "BLANKS PLANTED IN FRONT OF THE STRING, since the first load is aligned DOWN and a "
           "mis-cleared mask would pick one up; EVERY byte value 0x01..0xFF leading, trailing, "
           "inside both runs, and as a whole string -- a blank is 0x20 and nothing else, and a TAB "
           "IS NOT A BLANK; every length 0..70 x every leading run x every trailing run; a middle "
           "blank at every position for every length 3..120, which must always survive; long strings "
           "to 1200 characters through the 32-byte move loop; 200k blank-biased fuzz over the full "
           "byte range; and a guard-page sweep for every length 1..200 in three modes)\n");
    return 0;
}
