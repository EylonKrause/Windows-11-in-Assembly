/* changes/238-pathmakeprettya/probes/pmpa.c
   Pin down shlwapi!PathMakePrettyA before writing any assembly.

   WHY. discovery/shlwapi_path3.c measured 1900.95 ns for 254 characters -- 7.48 ns PER BYTE, about
   21 cycles a byte -- against 444.80 for the wide form on the same character count. That is 4.3x the
   wide cost for HALF the bytes: 8.5x per byte, the widest narrow/wide gap in the whole survey. It is
   also the survey's largest EARLY-vs-FULL gap at 98.6x, because the shared early subject is mixed
   case and gets refused in a few bytes while an all-uppercase path is rewritten end to end.

   WHAT HAS TO BE SETTLED, and none of it can be guessed:

     1. THE TRIGGER. "Make pretty" is documented as lowercasing a path that is all uppercase, which
        immediately raises the question of what "all uppercase" means for the bytes that are NEITHER
        upper nor lower -- digits, separators, punctuation, the whole 0x80..0xFF range. Does one digit
        veto the rewrite? Does one lowercase letter? Does a separator? Each answer is a different
        predicate, and the difference is invisible on a corpus of plausible paths.

     2. THE MAPPING, over all 256 byte values. This is a case fold, which is the trap change 232
        documented: the narrow and wide forms of a path function need not agree on which bytes are
        letters, and inheriting the wide set would have wrongly touched 78 byte values there. So the
        mapping is DERIVED here, byte by byte, from the narrow export -- never from CharLowerA, never
        from the wide form, never from the CP1252 tables.

     3. WHETHER THE LENGTH CAN CHANGE. A case fold that went through UTF-16 could in principle come
        back a different number of bytes. If it can, no in-place byte loop reproduces it.

     4. THE RETURN. The A and W forms are declared differently in different headers -- void in some,
        BOOL in others -- so the return is read off the live export rather than from a header.

     5. WHAT IT WRITES WHEN IT REFUSES. A poison fill is the only way to tell "wrote nothing" from
        "wrote the same bytes back", and change 226 is the precedent: it writes a SECOND terminator
        that a string comparison cannot see.

     6. NULL, and a length bound if there is one -- change 236's MAX_PATH rule got past six probes
        because length was never one of the enumerated dimensions.

   Nothing here writes to disk or touches system state. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* read the return as an int; if the function is really void this is simply ignored by the callee */
typedef int (WINAPI *PMP)(char*);
static PMP pmp;

#define POISON 0xCD
static char buf[8192];

/* Run one case and describe the WHOLE observable result. */
static void show(const char* in, const char* tag)
{
    int n = (int)strlen(in);
    memset(buf, POISON, 512);
    memcpy(buf, in, n + 1);
    int r = pmp(buf);
    int touched = -1, last = -1;
    for (int i = 0; i < 512; ++i) {
        unsigned char c = (unsigned char)buf[i];
        int orig = (i <= n) ? (unsigned char)in[i] : POISON;
        if (c != orig) { if (touched < 0) touched = i; last = i; }
    }
    printf("  %-28s -> ret %2d  \"%s\"%s", tag, r, buf,
           touched < 0 ? "   (unchanged)" : "");
    if (touched >= 0) printf("   changed indices %d..%d", touched, last);
    printf("\n");
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    pmp = (PMP)GetProcAddress(hs, "PathMakePrettyA");
    if (!pmp) { printf("cannot resolve PathMakePrettyA\n"); return 1; }
    printf("PathMakePrettyA = %p\nGetACP() = %u\n", (void*)pmp, GetACP());
    {
        CPINFO ci; int lead = 0, i;
        if (GetCPInfo(CP_ACP, &ci))
            for (i = 0; i < MAX_LEADBYTES && ci.LeadByte[i]; i += 2)
                lead += ci.LeadByte[i+1] - ci.LeadByte[i] + 1;
        printf("DBCS lead bytes in the active code page: %d\n\n", lead);
    }

    printf("=== 1. the obvious cases ===\n");
    show("C:\\DIR\\FILE.TXT", "all uppercase");
    show("C:\\dir\\file.txt", "all lowercase");
    show("C:\\Dir\\FILE.TXT", "mixed");
    show("C:\\DIR\\FILE.TXT ", "uppercase + trailing space");
    show("ABC",               "no path punctuation");
    show("abc",               "lowercase, no punctuation");
    show("A",                 "one uppercase letter");
    show("a",                 "one lowercase letter");
    show("",                  "the empty string");
    show("C:\\DIR1\\FILE2.TXT", "uppercase WITH DIGITS");
    show("123\\456",          "digits only");
    show("C:\\DIR\\FILE.TXT",  "again (idempotence check)");

    printf("\n=== 2. THE TRIGGER: which single byte vetoes the rewrite? ===\n");
    printf("  Subject \"AB?DEF\" with ? swept over every byte value. If the rewrite still happens,\n");
    printf("  that byte does not veto it. Reported as the two SETS, because the boundary is the rule.\n");
    {
        int veto = 0, ok = 0;
        int veto_list[300], nveto = 0;
        for (int v = 1; v < 256; ++v) {
            char s[16];
            s[0]='A'; s[1]='B'; s[2]=(char)v; s[3]='D'; s[4]='E'; s[5]='F'; s[6]=0;
            memcpy(buf, s, 7);
            pmp(buf);
            /* did the surrounding uppercase letters get lowercased? */
            int rewritten = (buf[0] == 'a');
            if (rewritten) ++ok;
            else { ++veto; if (nveto < 300) veto_list[nveto++] = v; }
        }
        printf("    %d of 255 byte values VETO the rewrite, %d allow it\n", veto, ok);
        printf("    the vetoing set:");
        for (int i = 0; i < nveto; ++i) {
            int v = veto_list[i];
            if (v >= 32 && v < 127) printf(" %02X('%c')", v, v); else printf(" %02X", v);
        }
        printf("\n");
    }

    printf("\n=== 3. THE MAPPING, derived byte by byte from the narrow export ===\n");
    printf("  For each byte value v, a subject that is otherwise uppercase-only, so the rewrite is\n");
    printf("  guaranteed to fire: \"A<v>A\" -- unless v itself vetoes, in which case v is untestable\n");
    printf("  this way and is reported separately.\n");
    {
        unsigned char map[256];
        int untestable[300], nun = 0;
        for (int v = 0; v < 256; ++v) map[v] = (unsigned char)v;
        for (int v = 1; v < 256; ++v) {
            char s[8];
            s[0]='A'; s[1]=(char)v; s[2]='A'; s[3]=0;
            memcpy(buf, s, 4);
            pmp(buf);
            if (buf[0] != 'a') { if (nun < 300) untestable[nun++] = v; continue; }
            map[v] = (unsigned char)buf[1];
        }
        int moved = 0, plus20 = 0, other = 0;
        for (int v = 1; v < 256; ++v) if (map[v] != v) ++moved;
        printf("    %d of 255 byte values are REWRITTEN; %d could not be tested (they veto)\n",
               moved, nun);
        printf("    the exceptions to \"+0x20\":\n");
        for (int v = 1; v < 256; ++v) {
            if (map[v] == v) continue;
            if (map[v] == v + 0x20) { ++plus20; continue; }
            printf("      %02X -> %02X   (delta %+d)\n", v, map[v], map[v] - v);
            ++other;
        }
        printf("    %d map by exactly +0x20; %d do not\n", plus20, other);
        printf("    contiguous ranges that map by +0x20:\n");
        {
            int start = -1;
            for (int v = 1; v <= 256; ++v) {
                int f = (v < 256) && (map[v] == v + 0x20);
                if (f && start < 0) start = v;
                else if (!f && start >= 0) { printf("      %02X..%02X\n", start, v-1); start = -1; }
            }
        }
        if (nun) {
            printf("    untestable (vetoing) values:");
            for (int i = 0; i < nun; ++i) printf(" %02X", untestable[i]);
            printf("\n");
        }
    }

    printf("\n=== 4. CAN THE LENGTH CHANGE? ===\n");
    printf("  Every byte value, in an otherwise-uppercase subject, checking strlen before and after\n");
    printf("  and checking that the terminator did not move.\n");
    {
        int changed = 0;
        for (int v = 1; v < 256; ++v) {
            char s[8];
            s[0]='A'; s[1]=(char)v; s[2]='A'; s[3]=0;
            memset(buf, POISON, 32);
            memcpy(buf, s, 4);
            int before = 3;
            pmp(buf);
            int after = (int)strlen(buf);
            if (after != before || (unsigned char)buf[4] != POISON) {
                ++changed;
                if (changed <= 8)
                    printf("    LENGTH MOVED for %02X: %d -> %d, buf[4] = %02X\n",
                           v, before, after, (unsigned char)buf[4]);
            }
        }
        printf("    %d of 255 byte values moved the length or the terminator\n", changed);
    }

    printf("\n=== 5. NULL ===\n");
    {
        __try { printf("    PathMakePrettyA(NULL) -> %d\n", pmp(0)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { printf("    PathMakePrettyA(NULL) FAULTED\n"); }
    }

    printf("\n=== 6. WHAT DOES IT WRITE WHEN IT REFUSES? ===\n");
    printf("  A mixed-case subject in a poison field: does it write the same bytes back, write a\n");
    printf("  redundant terminator, or touch nothing at all?\n");
    {
        static const char* V[] = { "C:\\Dir\\File.txt", "abc", "a", "", "AbC", 0 };
        for (int i = 0; V[i]; ++i) {
            int n = (int)strlen(V[i]);
            memset(buf, POISON, 64);
            memcpy(buf, V[i], n + 1);
            pmp(buf);
            int touched = 0;
            for (int k = n + 1; k < 64; ++k)
                if ((unsigned char)buf[k] != POISON) { touched = 1; break; }
            printf("    \"%-16s\" -> \"%s\"%s\n", V[i], buf,
                   touched ? "   WROTE PAST THE TERMINATOR" : "");
        }
    }

    printf("\n=== 7. LENGTH AS A DIMENSION -- is there a bound? ===\n");
    printf("  (change 236's MAX_PATH rule got past six probes because length was never enumerated)\n");
    {
        int firstfail = -1;
        for (int n = 1; n <= 700; ++n) {
            for (int i = 0; i < n; ++i) buf[i] = (i % 8 == 7) ? '\\' : (char)('A' + i % 23);
            buf[n] = 0;
            pmp(buf);
            /* every letter should now be lowercase */
            int ok = 1;
            for (int i = 0; i < n; ++i) {
                char c = buf[i];
                if (c == '\\') continue;
                if (c < 'a' || c > 'z') { ok = 0; break; }
            }
            if (!ok && firstfail < 0) firstfail = n;
        }
        if (firstfail < 0) printf("    lengths 1..700: every one was fully rewritten, no bound\n");
        else               printf("    FIRST LENGTH NOT FULLY REWRITTEN: %d\n", firstfail);
    }

    printf("\n=== 8. does it read or write past the terminator at a page edge? ===\n");
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        int ok = 0, faults = 0;
        for (int tail = 2; tail <= 200; ++tail) {
            for (int shape = 0; shape < 2; ++shape) {
                char* p = (base+pg) - tail;
                for (int i = 0; i < tail-1; ++i)
                    p[i] = shape ? (char)('A' + i % 23) : (char)('a' + i % 23);
                p[tail-1] = 0;
                __try { pmp(p); ++ok; }
                __except (EXCEPTION_EXECUTE_HANDLER) { ++faults; }
            }
        }
        printf("    over %d guard-page cases (uppercase and lowercase): %d ok, %d faulted\n",
               ok + faults, ok, faults);
        VirtualFree(base, 0, MEM_RELEASE);
    }
    return 0;
}
