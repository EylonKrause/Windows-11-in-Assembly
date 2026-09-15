/* changes/241-pathcchaddbackslashex/probes/pcabsx.c
   Pin down kernelbase!PathCchAddBackslashEx and PathCchRemoveBackslashEx.

   WHY THESE TWO, AND WHY TOGETHER. discovery/kernelbase_pathcch.c measured both at 0.101 ns per byte
   on a 1000-character path once the 13.13 ns restore is subtracted -- 202 ns for work that is
   "find the end, then maybe write one character". The plain forms PathCchAddBackslash and
   PathCchRemoveBackslash are already landed in this project (changes 175-ish era, listed in the
   image); these are the Ex forms, which additionally hand back a pointer to the new end of the string
   and the count remaining in the buffer. They are the same computation plus two output parameters, so
   one probe covers both and one change can carry them.

   WHAT CHANGE 240 ALREADY SETTLED, and what must NOT be assumed from it. 240 pinned this family's
   protected-root rule exactly, including the clause no documentation produces -- an EMPTY SHARE falls
   back to the end of the SERVER -- and that a drive letter is 114 wchar values rather than 52. Those
   were derived from PathCchRemoveFileSpec, and the whole lesson of change 232 (and of 240 itself,
   where PathCchSkipRoot turned out to be the wrong source) is that a rule shared across functions has
   to be audited against what EACH ONE computes. So this probe re-derives the root behaviour for these
   two exports rather than inheriting it, by the same fixed-point trick:

       RemoveBackslashEx applied until it stops changing anything IS the protected prefix.

   WHAT HAS TO BE SETTLED:

     1. WHEN does AddBackslashEx add one, and when does it decline? An empty string, a string already
        ending in a separator, a bare root, a relative path.
     2. WHAT are ppszEnd and pcchRemaining when it declines, and when either is NULL? A function that
        writes its out-parameters on the failure path is a different function from one that does not.
     3. THE cch RULE. 240 found that cch bounds the HIGHEST INDEX WRITTEN, which was neither
        "result+1" nor "input+1". These append rather than truncate, so the boundary is in a different
        place and is measured here, not carried over.
     4. Which HRESULTs appear. 240 used S_OK / S_FALSE / E_INVALIDARG; an appending function can also
        plausibly return STRSAFE_E_INSUFFICIENT_BUFFER, so the exact values are read off the export.
     5. RemoveBackslashEx: does it refuse to cut into the root, and is that root the SAME one
        RemoveFileSpec protects? 240's root came from RemoveFileSpec and there is no reason yet to
        believe another function in the family shares it.
     6. NULL, and overread.

   Nothing here writes to disk or touches system state. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PATHCCH_MAX_CCH 0x8000

typedef HRESULT (WINAPI *PEX)(PWSTR, size_t, PWSTR*, size_t*);
typedef HRESULT (WINAPI *P2)(PWSTR, size_t);
static PEX addx, remx;
static P2  addp, remp;

#define POISON 0xCD
static wchar_t buf[4096];

static const char* hrname(HRESULT hr)
{
    static char t[32];
    if (hr == S_OK) return "S_OK   ";
    if (hr == S_FALSE) return "S_FALSE";
    if (hr == E_INVALIDARG) return "E_INVAL";
    if (hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)) return "E_BUF  ";
    if ((unsigned long)hr == 0x8007007Aul) return "E_BUF  ";
    sprintf(t, "%08lX", (unsigned long)hr);
    return t;
}

/* one AddBackslashEx call, described completely */
static void showa(const wchar_t* in, size_t cch, int pass_out, const char* tag)
{
    int n = (int)wcslen(in);
    for (int i = 0; i < 4096; ++i) buf[i] = 0xCDCD;
    memcpy(buf, in, (size_t)(n + 1) * 2);
    PWSTR  e = (PWSTR)(size_t)0xDEAD;
    size_t r = (size_t)0xDEAD;
    HRESULT hr = pass_out ? addx(buf, cch, &e, &r) : addx(buf, cch, 0, 0);
    printf("  %-22s cch=%-7zu -> %s  \"%ls\"", tag, cch, hrname(hr), buf);
    if (pass_out) {
        if ((size_t)e == 0xDEAD) printf("   end=UNTOUCHED");
        else if (!e)             printf("   end=NULL");
        else                     printf("   end=+%d", (int)(e - buf));
        if (r == (size_t)0xDEAD) printf("  rem=UNTOUCHED");
        else                     printf("  rem=%zu", r);
    }
    /* how far did the poison move? */
    int touched = -1;
    for (int i = 0; i < 40; ++i) {
        wchar_t orig = (i <= n) ? in[i] : (wchar_t)0xCDCD;
        if (buf[i] != orig) { touched = i; break; }
    }
    if (touched >= 0) printf("   first changed %d", touched);
    printf("\n");
}

static void showr(const wchar_t* in, size_t cch, const char* tag)
{
    int n = (int)wcslen(in);
    for (int i = 0; i < 4096; ++i) buf[i] = 0xCDCD;
    memcpy(buf, in, (size_t)(n + 1) * 2);
    PWSTR  e = (PWSTR)(size_t)0xDEAD;
    size_t r = (size_t)0xDEAD;
    HRESULT hr = remx(buf, cch, &e, &r);
    printf("  %-22s cch=%-7zu -> %s  \"%ls\"", tag, cch, hrname(hr), buf);
    if ((size_t)e == 0xDEAD) printf("   end=UNTOUCHED"); else if (!e) printf("   end=NULL");
    else printf("   end=+%d", (int)(e - buf));
    if (r == (size_t)0xDEAD) printf("  rem=UNTOUCHED"); else printf("  rem=%zu", r);
    printf("\n");
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    addx = (PEX)GetProcAddress(hk, "PathCchAddBackslashEx");
    remx = (PEX)GetProcAddress(hk, "PathCchRemoveBackslashEx");
    addp = (P2)GetProcAddress(hk, "PathCchAddBackslash");
    remp = (P2)GetProcAddress(hk, "PathCchRemoveBackslash");
    if (!addx || !remx) { printf("cannot resolve the Ex forms\n"); return 1; }
    printf("AddBackslashEx=%p RemoveBackslashEx=%p\nS_OK=0 S_FALSE=1 E_INVALIDARG=%08lX "
           "E_BUF=%08lX\n\n", (void*)addx, (void*)remx,
           (unsigned long)E_INVALIDARG,
           (unsigned long)HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));

    printf("=== 1. AddBackslashEx: when does it add, and what comes back? ===\n");
    {
        static const wchar_t* V[] = {
            L"C:\\dir", L"C:\\dir\\", L"C:\\", L"C:", L"\\", L"\\\\", L"\\\\srv",
            L"\\\\srv\\shr", L"\\\\srv\\shr\\", L"dir", L"dir\\", L"", L"a", L"a\\",
            L"\\\\?\\C:", L"\\\\?\\C:\\", L"\\\\?\\UNC\\s\\h", 0
        };
        for (int i = 0; V[i]; ++i) {
            char tag[48]; sprintf(tag, "\"%.18ls\"", V[i]);
            showa(V[i], PATHCCH_MAX_CCH, 1, tag);
        }
    }

    printf("\n=== 2. AddBackslashEx with the out-parameters omitted, and one of each ===\n");
    {
        showa(L"C:\\dir", PATHCCH_MAX_CCH, 0, "both NULL");
        int n = 6;
        for (int i = 0; i < 4096; ++i) buf[i] = 0xCDCD;
        memcpy(buf, L"C:\\dir", (size_t)(n + 1) * 2);
        PWSTR e = (PWSTR)(size_t)0xDEAD;
        HRESULT hr = addx(buf, PATHCCH_MAX_CCH, &e, 0);
        printf("  end only            -> %s \"%ls\"  end=+%d\n", hrname(hr), buf,
               (size_t)e == 0xDEAD ? -1 : (int)(e - buf));
        for (int i = 0; i < 4096; ++i) buf[i] = 0xCDCD;
        memcpy(buf, L"C:\\dir", (size_t)(n + 1) * 2);
        size_t r = (size_t)0xDEAD;
        hr = addx(buf, PATHCCH_MAX_CCH, 0, &r);
        printf("  remaining only      -> %s \"%ls\"  rem=%zu\n", hrname(hr), buf, r);
    }

    printf("\n=== 3. THE cch BOUNDARY for AddBackslashEx ===\n");
    printf("  A 6-character path that wants to become 7. Change 240 found cch bounding the HIGHEST\n");
    printf("  INDEX WRITTEN there; an appending function may well differ, so it is swept.\n");
    {
        for (size_t cch = 0; cch <= 12; ++cch) showa(L"C:\\dir", cch, 1, "C:\\dir");
        printf("  and a path that already ends in a separator, so nothing needs to be written:\n");
        for (size_t cch = 0; cch <= 10; ++cch) showa(L"C:\\dir\\", cch, 1, "C:\\dir\\");
        printf("  at and past the documented maximum:\n");
        showa(L"C:\\dir", PATHCCH_MAX_CCH, 1, "at max");
        showa(L"C:\\dir", PATHCCH_MAX_CCH + 1, 1, "past max");
        showa(L"C:\\dir", (size_t)-1, 1, "SIZE_MAX");
    }

    printf("\n=== 4. RemoveBackslashEx: the same questions ===\n");
    {
        static const wchar_t* V[] = {
            L"C:\\dir\\", L"C:\\dir", L"C:\\", L"C:", L"\\", L"\\\\", L"\\\\srv\\",
            L"\\\\srv\\shr\\", L"\\\\srv\\shr", L"dir\\", L"dir", L"", L"a\\", L"a\\\\",
            L"\\\\?\\C:\\", L"\\\\?\\UNC\\s\\h\\", 0
        };
        for (int i = 0; V[i]; ++i) {
            char tag[48]; sprintf(tag, "\"%.18ls\"", V[i]);
            showr(V[i], PATHCCH_MAX_CCH, tag);
        }
        printf("  and the cch sweep:\n");
        for (size_t cch = 0; cch <= 10; ++cch) showr(L"C:\\dir\\", cch, "C:\\dir\\");
    }

    printf("\n=== 5. DOES RemoveBackslashEx PROTECT THE SAME ROOT AS RemoveFileSpec? ===\n");
    printf("  Change 240 derived the protected root from PathCchRemoveFileSpec. Inheriting it here\n");
    printf("  would be the change 232 mistake, so it is re-derived by the same fixed-point trick and\n");
    printf("  the two are printed side by side. Only the disagreements matter.\n");
    {
        static const wchar_t AL[4] = { L'a', L'\\', L':', L'?' };
        wchar_t s[16], w[32];
        long total = 0, diff = 0;
        int shown = 0;
        for (int len = 0; len <= 6; ++len) {
            long combos = 1; for (int i = 0; i < len; ++i) combos *= 4;
            for (long c = 0; c < combos; ++c) {
                long v = c;
                for (int i = 0; i < len; ++i) { s[i] = AL[v % 4]; v /= 4; }
                s[len] = 0;
                /* the fixed point of RemoveBackslashEx */
                memcpy(w, s, (size_t)(len + 1) * 2);
                for (int g = 0; g < 40; ++g) {
                    PWSTR e; size_t r;
                    if (remx(w, PATHCCH_MAX_CCH, &e, &r) != S_OK) break;
                }
                int rb = (int)wcslen(w);
                /* the fixed point of RemoveFileSpec, which is change 240's root */
                wchar_t w2[32];
                memcpy(w2, s, (size_t)(len + 1) * 2);
                HRESULT (WINAPI *prfs)(PWSTR, size_t) =
                    (HRESULT (WINAPI *)(PWSTR, size_t))GetProcAddress(
                        GetModuleHandleW(L"kernelbase.dll"), "PathCchRemoveFileSpec");
                for (int g = 0; g < 40; ++g) if (prfs(w2, PATHCCH_MAX_CCH) != S_OK) break;
                int rf = (int)wcslen(w2);
                if (rb != rf) {
                    ++diff;
                    if (shown < 20) {
                        printf("    \"%-7ls\" RemoveBackslash fixed point=%d (\"%ls\"), "
                               "RemoveFileSpec=%d (\"%ls\")\n", s, rb, w, rf, w2);
                        ++shown;
                    }
                }
                ++total;
            }
        }
        printf("\n    %ld strings: the two fixed points differ on %ld\n", total, diff);
        printf("    => %s\n", diff ? "they protect DIFFERENT prefixes -- do not share the rule"
                                   : "they agree here, but the rule is still re-derived rather than "
                                     "assumed");
    }

    printf("\n=== 6. do the Ex forms agree with the plain ones on the string? ===\n");
    if (addp && remp) {
        static const wchar_t* V[] = {
            L"C:\\dir", L"C:\\dir\\", L"C:\\", L"C:", L"\\", L"\\\\srv\\shr", L"dir", L"", 0
        };
        for (int i = 0; V[i]; ++i) {
            int n = (int)wcslen(V[i]);
            wchar_t a[64], b[64];
            memcpy(a, V[i], (size_t)(n + 1) * 2);
            memcpy(b, V[i], (size_t)(n + 1) * 2);
            PWSTR e; size_t r;
            HRESULT h1 = addx(a, PATHCCH_MAX_CCH, &e, &r);
            HRESULT h2 = addp(b, PATHCCH_MAX_CCH);
            printf("  add  %-14ls Ex -> %s \"%ls\"   plain -> %s \"%ls\"%s\n", V[i],
                   hrname(h1), a, hrname(h2), b,
                   (h1 != h2 || wcscmp(a, b)) ? "   <== DIFFER" : "");
            memcpy(a, V[i], (size_t)(n + 1) * 2);
            memcpy(b, V[i], (size_t)(n + 1) * 2);
            h1 = remx(a, PATHCCH_MAX_CCH, &e, &r);
            h2 = remp(b, PATHCCH_MAX_CCH);
            printf("  rem  %-14ls Ex -> %s \"%ls\"   plain -> %s \"%ls\"%s\n", V[i],
                   hrname(h1), a, hrname(h2), b,
                   (h1 != h2 || wcscmp(a, b)) ? "   <== DIFFER" : "");
        }
    }

    printf("\n=== 7. NULL and overread ===\n");
    {
        PWSTR e; size_t r;
        printf("  addx(NULL,max,&e,&r) -> %s\n", hrname(addx(0, PATHCCH_MAX_CCH, &e, &r)));
        printf("  remx(NULL,max,&e,&r) -> %s\n", hrname(remx(0, PATHCCH_MAX_CCH, &e, &r)));
        SYSTEM_INFO si; GetSystemInfo(&si);
        SIZE_T pg = si.dwPageSize;
        char* base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        DWORD old; VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
        int ok = 0, faults = 0;
        for (int tail = 4; tail <= 200; tail += 2) {
            wchar_t* p = (wchar_t*)((base+pg) - tail*2);
            for (int i = 0; i < tail-1; ++i) p[i] = (i % 8 == 7) ? L'\\' : (wchar_t)(L'a' + i % 23);
            p[tail-1] = 0;
            __try { remx(p, (size_t)tail, &e, &r); ++ok; }
            __except (EXCEPTION_EXECUTE_HANDLER) { ++faults; }
        }
        printf("  RemoveBackslashEx over %d guard-page cases: %d ok, %d faulted\n",
               ok + faults, ok, faults);
        VirtualFree(base, 0, MEM_RELEASE);
    }
    return 0;
}
