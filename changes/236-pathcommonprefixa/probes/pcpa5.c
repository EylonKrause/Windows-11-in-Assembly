/* changes/236-pathcommonprefixa/probes/pcpa5.c
   The two gaps pcpa4.c found, isolated.

   pcpa4.c ran the full model against the live export over 3.6 million pairs and found 3145
   mismatches. They are not noise and they are not one bug: they are two rules the first four probes
   did not reach.

   GAP 1 -- THE LENGTH-TWO ANSWER IS THREE.

       PathCommonPrefixA("aa", "aa", out)  ->  3

       on a string that is TWO characters long. All nine length-2 strings over {a, backslash, colon}
       do it, and no string of any other length does. The obvious reading is that the function treats
       a 2-character common prefix as a drive specification and extends it to a root -- but "aa" has
       no colon, so once again the test is POSITIONAL. What matters for the assembly is what ends up
       IN THE BUFFER, and there are two very different possibilities:

         (a) it WRITES a third character it invented -- a backslash -- making "aa\";
         (b) it COPIES a third character from the caller's string, i.e. it reads PAST THE TERMINATOR.

       (a) is reproducible. (b) is a read past the end of a caller's buffer, and this probe checks it
       directly by putting a 2-character string so that its terminator is the last readable byte
       before a PAGE_NOACCESS page. If the shipped export faults there, it really does overread, and
       that is a finding about the shipped function rather than something to reproduce.

   GAP 2 -- "ONE PATH ENDS WHERE THE OTHER HAS A SEPARATOR" IS NOT ALWAYS UNCUT.

       PathCommonPrefixA("\", "\\")  ->  0,  where the model said 1.

       but

       PathCommonPrefixA("a", "a\")  ->  1.

       Both have the shorter path ending exactly where the longer one continues with a separator, and
       they get different answers. So the rule is conditional on something about the prefix itself.
       Rather than guess again, this probe ISOLATES that decision the way pcpa3.c isolated the
       truncation: for every P, compare pcp(P, P + c) against both candidate answers -- len(P) and
       trunc(P) -- and enumerate which one wins. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef int (WINAPI *PCP)(LPCSTR, LPCSTR, LPSTR);
static PCP pcp;

#define POISON 0xCD
static char out[4096];

static int trunc_of(const char* p)
{
    char a[40], b[40];
    int n = (int)strlen(p);
    memcpy(a, p, n); a[n] = 'x'; a[n+1] = 0;
    memcpy(b, p, n); b[n] = 'y'; b[n+1] = 0;
    return pcp(a, b, NULL);
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pcp = (PCP)GetProcAddress(hs, "PathCommonPrefixA");
    if (!pcp) { printf("cannot resolve PathCommonPrefixA\n"); return 1; }
    printf("GetACP() = %u\n\n", GetACP());

    /* ================= GAP 1 ==================================================== */
    printf("=== GAP 1a. what does the length-2 case actually WRITE? ===\n");
    {
        static const char* V[] = { "aa", "C:", "a\\", "\\\\", "ab", "zz", 0 };
        for (int i = 0; V[i]; ++i) {
            memset(out, POISON, 32);
            int r = pcp(V[i], V[i], out);
            printf("  pcp(\"%s\",\"%s\") -> %d   buffer bytes:", V[i], V[i], r);
            for (int k = 0; k < 5; ++k) {
                unsigned char c = (unsigned char)out[k];
                if (c == POISON) printf("  --");
                else if (c == 0)  printf("  00");
                else printf("  %02X", c);
            }
            printf("   \"%s\"\n", out);
        }
        printf("\n  => if the third byte is 5C the function INVENTS a backslash;\n");
        printf("     if it is anything else it COPIED a byte from past the terminator.\n");
    }

    printf("\n=== GAP 1b. does it read past the terminator of a 2-character string? ===\n");
    printf("  A 2-character string placed so its NUL is the LAST readable byte before a\n");
    printf("  PAGE_NOACCESS page. An overread faults; inventing a backslash does not.\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        char* p = (base+pg) - 3;               /* 'a' 'a' NUL, NUL is the last byte of the page */
        p[0] = 'a'; p[1] = 'a'; p[2] = 0;
        int faulted = 0, r = -1;
        __try { memset(out, POISON, 32); r = pcp(p, p, out); }
        __except (EXCEPTION_EXECUTE_HANDLER) { faulted = 1; }
        if (faulted) printf("  FAULTED -- the shipped export reads past the terminator here\n");
        else {
            printf("  no fault; returned %d, buffer bytes:", r);
            for (int k = 0; k < 5; ++k) {
                unsigned char c = (unsigned char)out[k];
                if (c == POISON) printf("  --"); else printf("  %02X", c);
            }
            printf("\n");
        }
        /* and the same with only ONE argument at the guard */
        {
            static char q[8]; q[0]='a'; q[1]='a'; q[2]=0;
            int f2 = 0; int r2 = -1;
            __try { memset(out, POISON, 32); r2 = pcp(p, q, out); }
            __except (EXCEPTION_EXECUTE_HANDLER) { f2 = 1; }
            printf("  first path at the guard, second in a normal buffer: %s (returned %d)\n",
                   f2 ? "FAULTED" : "no fault", r2);
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    printf("\n=== GAP 1c. is it really only length 2? ===\n");
    {
        static char s[64];
        for (int n = 0; n <= 8; ++n) {
            for (int i = 0; i < n; ++i) s[i] = 'a';
            s[n] = 0;
            printf("  len %d: pcp(s,s) = %d%s\n", n, pcp(s, s, NULL),
                   pcp(s, s, NULL) != n ? "   <== not its own length" : "");
        }
    }

    /* ================= GAP 2 ==================================================== */
    printf("\n=== GAP 2. when one path ends where the other continues ===\n");
    printf("  For every P, pcp(P, P+c) is compared against len(P) and against trunc(P).\n");
    printf("  Enumerated over {a, backslash, colon} to length 6, with c over the same alphabet.\n");
    {
        static const char AL[3] = { 'a', '\\', ':' };
        char p[16], q[18];
        long total = 0, is_len = 0, is_trunc = 0, is_both = 0, is_neither = 0;
        int shown = 0;
        for (int lp = 0; lp <= 6; ++lp) {
            long cp = 1; for (int i = 0; i < lp; ++i) cp *= 3;
            for (long kp = 0; kp < cp; ++kp) {
                long v = kp;
                for (int i = 0; i < lp; ++i) { p[i] = AL[v % 3]; v /= 3; }
                p[lp] = 0;
                int tr = trunc_of(p);
                for (int ci = 0; ci < 3; ++ci) {
                    memcpy(q, p, lp); q[lp] = AL[ci]; q[lp+1] = 0;
                    int r = pcp(p, q, NULL);
                    ++total;
                    int el = (r == lp), et = (r == tr);
                    if (el && et) ++is_both;
                    else if (el)  ++is_len;
                    else if (et)  ++is_trunc;
                    else {
                        ++is_neither;
                        if (shown < 20) {
                            printf("    NEITHER  P=\"%s\" c='%c'  -> %d   (len %d, trunc %d)\n",
                                   p, AL[ci], r, lp, tr);
                            ++shown;
                        }
                    }
                }
            }
        }
        printf("\n    %ld cases: %ld both agree, %ld == len(P) only, %ld == trunc(P) only, "
               "%ld neither\n", total, is_both, is_len, is_trunc, is_neither);
    }

    printf("\n=== GAP 2b. which P take len(P), split by the continuation character ===\n");
    printf("  (c == backslash is the whole-component case; the others are ordinary divergences)\n");
    {
        static const char AL[3] = { 'a', '\\', ':' };
        char p[16], q[18];
        for (int ci = 0; ci < 3; ++ci) {
            long total = 0, el = 0, et = 0;
            long lenonly_shown = 0;
            printf("  c = '%c':\n", AL[ci]);
            for (int lp = 0; lp <= 6; ++lp) {
                long cp = 1; for (int i = 0; i < lp; ++i) cp *= 3;
                for (long kp = 0; kp < cp; ++kp) {
                    long v = kp;
                    for (int i = 0; i < lp; ++i) { p[i] = AL[v % 3]; v /= 3; }
                    p[lp] = 0;
                    int tr = trunc_of(p);
                    memcpy(q, p, lp); q[lp] = AL[ci]; q[lp+1] = 0;
                    int r = pcp(p, q, NULL);
                    ++total;
                    if (r == lp) ++el;
                    if (r == tr) ++et;
                    /* list the ones where c is a separator but the answer is NOT len(P) */
                    if (AL[ci] == '\\' && r != lp && lenonly_shown < 20) {
                        printf("      P=\"%s\" -> %d, but len(P) = %d (trunc %d)\n", p, r, lp, tr);
                        ++lenonly_shown;
                    }
                }
            }
            printf("      %ld cases: %ld took len(P), %ld took trunc(P)\n", total, el, et);
        }
    }
    return 0;
}
