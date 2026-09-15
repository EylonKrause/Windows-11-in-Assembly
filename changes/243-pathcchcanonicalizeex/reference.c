/* changes/243-pathcchcanonicalizeex/reference.c
   An INDEPENDENT model of kernelbase!PathCchCanonicalizeEx for dwFlags == 0, used as the oracle in
   correctness.c. It shares nothing with impl.asm.

   THE DOMAIN IS dwFlags == 0, and that is the domain impl.asm implements too. Two reasons, both
   measured rather than assumed:

     * It is the only value that occurs. PathCchCanonicalize is documented as PathCchCanonicalizeEx with
       PATHCCH_NONE, so every caller of the simple form lands here, and every caller in this project's
       corpus passes 0.
     * FLAG 0x01 IS NOT A POST-STEP -- IT CHANGES THE POP ITSELF. probes/refcheck.c measured
       "C:a\.." as "\" with flags 0 and "C:a\" with 0x01, "\\srv\.." as "\" against "\\srv\", and
       "\\srv\shr\a\..\..\.." as "\" against "\\srv\". So ALLOW_LONG_PATHS selects a different backward
       walk with a different floor, which is a second contract, not a modifier of this one. The
       disassembly agrees: `and eax, 5 / cmp al, 5` at 0x11203 and 0x11441 branch to separate code.

   Everything outside the domain is handled by impl.asm tail-jumping to the original implementation
   (see the fallback pointer in impl.asm), so the flagged paths behave identically by construction
   rather than by reimplementation.

   THE CONTRACT, measured. probes/model.c agrees with the live export on 11,772,366 enumerated and
   random paths with 0 mismatches; probes/isroot.c identified the internal root predicate as the
   exported PathCchIsRoot over 8,587 strings with 0 differences; RESULTS.md carries the derivation and
   the disassembly that settled it. In brief: one linear walk with a write cursor, dispatched on the
   LENGTH of each component -- 0 emits one separator, 1 that is "." skips itself and the separator after
   it, 2 that is ".." pops by walking the output backwards, anything else is copied verbatim. There is
   no component list, no stack and no root copy, and no protected root: the drive of "C:\a\.." survives
   only because the finish turns a two-character output ending in ':' back into "C:\".

   Nothing here creates, opens or stats any file, and nothing here calls the function it models. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <wchar.h>

#define REF_OK        ((long)0x00000000L)
#define REF_INVALID   ((long)0x80070057L)    /* E_INVALIDARG                  */
#define REF_BUF       ((long)0x8007007AL)    /* ERROR_INSUFFICIENT_BUFFER     */
#define REF_EXCED     ((long)0x800700CEL)    /* ERROR_FILENAME_EXCED_RANGE    */
#define REF_DELEGATED ((long)0x0BADF00DL)    /* outside the domain: the caller must compare with live */

#define REF_MAXCCH   0x8000u
#define REF_CAP      0x104u                  /* MAX_PATH, terminator included */
#define REF_MAXCOMP  0x100u

/* A DRIVE LETTER IS AN ISO-8859-1 LETTER, not an ASCII one and not a Unicode one. probes/letter.c put
   all 65536 code units in the drive position and measured exactly 114 accepted: A-Z, a-z, and
   U+00C0..U+00FF except U+00D7 and U+00F7, the multiplication and division signs. It is NOT
   IsCharAlphaW (47455 accepted, 47342 disagreements) and NOT C1_ALPHA, and CP-1252 letters outside
   Latin-1 such as U+0160 are rejected. PathCchIsRoot was measured to use the same set.
   Folding with 0x20 collapses all five ranges into two, because C0..DF fold onto E0..FF and D7 and F7
   fold together. */
static int ref_letter(wchar_t c)
{
    unsigned x = (unsigned)c | 0x20u;
    if (x - 0x61u <= 0x19u) return 1;                   /* a-z, and A-Z folded onto it */
    return (x - 0xE0u <= 0x1Fu) && x != 0xF7u;          /* the Latin-1 letters         */
}

static int ref_ieq4unc(const wchar_t* r)     /* "UNC\" case-insensitively */
{
    static const wchar_t U[4] = { L'U', L'N', L'C', L'\\' };
    for (int i = 0; i < 4; ++i) {
        wchar_t x = r[i];
        if (x >= L'a' && x <= L'z') x -= 32;
        if (x != U[i]) return 0;
    }
    return 1;
}

/* PathCchIsRoot, reproduced. probes/isroot.c measured this to be exactly the predicate the function
   consults on the output it has built so far. The output CAN still carry an extended prefix -- an input
   like "\\?\a\.." keeps it, because only a drive or UNC prefix is stripped -- so the prefix forms have
   to be here too. */
static int ref_isroot(const wchar_t* s)
{
    wchar_t w[REF_CAP + 16];
    size_t n = wcslen(s);
    const wchar_t* t = s;

    if (!n) return 0;
    if (n >= 4 && s[0]==L'\\' && s[1]==L'\\' && s[2]==L'?' && s[3]==L'\\') {
        const wchar_t* r = s + 4;
        size_t rl = n - 4;
        if (rl >= 2 && ref_letter(r[0]) && r[1]==L':') { t = r; }
        else if (rl >= 4 && ref_ieq4unc(r)) {
            w[0] = L'\\'; w[1] = L'\\';
            memcpy(w + 2, r + 4, (rl - 4 + 1) * sizeof(wchar_t));
            t = w;
        }
    }
    n = wcslen(t);
    if (t[0] == L'\\') {
        if (n >= 2 && t[1] == L'\\') {
            const wchar_t* r = t + 2;
            const wchar_t* s1;
            if (!*r) return 1;                          /* "\\"             */
            s1 = wcschr(r, L'\\');
            if (!s1) return 1;                          /* "\\server"       */
            if (!s1[1]) return 0;                       /* "\\server\"      */
            if (wcschr(s1 + 1, L'\\')) return 0;         /* deeper           */
            return 1;                                   /* "\\server\share" */
        }
        return n == 1;                                  /* "\"              */
    }
    if (n == 3 && ref_letter(t[0]) && t[1]==L':' && t[2]==L'\\') return 1;  /* "C:\" */
    return 0;
}

long wia_ref_pathcchcanonicalizeex(wchar_t* out, size_t cch, const wchar_t* in, unsigned long flags)
{
    wchar_t buf[REF_CAP + 16];
    const wchar_t* p;
    size_t ol = 0, usable;

    /* --- the entry checks, in the order the function applies them ------------------ */
    if ((size_t)(cch - 1) > 0x7FFFFFFEu) {              /* cch == 0, or absurdly large */
        if (cch) out[0] = 0;
        return REF_INVALID;
    }
    out[0] = 0;                                         /* the buffer is emptied FIRST */
    if (cch > REF_MAXCCH) return REF_INVALID;
    if (flags) return REF_DELEGATED;                    /* outside the domain */

    usable = (cch < REF_CAP) ? cch : REF_CAP;

/* the two errors say WHICH bound failed: the MAX_PATH cap, or the caller's cch */
#define REF_FULL() (out[0] = 0, (usable == REF_CAP) ? REF_EXCED : REF_BUF)

    /* --- the extended prefix, rewritten before the walk --------------------------- */
    p = in;
    {
        size_t n = wcslen(in);
        if (n >= 4 && in[0]==L'\\' && in[1]==L'\\' && in[2]==L'?' && in[3]==L'\\') {
            const wchar_t* r = in + 4;
            size_t rl = n - 4;
            /* a drive letter and a colon is enough; NOTHING is required after the colon */
            if (rl >= 2 && ref_letter(r[0]) && r[1]==L':') {
                p = r;
            } else if (rl >= 4 && ref_ieq4unc(r)) {
                /* "\\?\UNC\rest" walks exactly as "\\" + rest, because the two leading separators of
                   the rewritten string are themselves zero-length components that emit themselves */
                if (usable < 3) return REF_FULL();
                buf[0] = L'\\'; buf[1] = L'\\'; ol = 2;
                p = r + 4;
            }
        }
    }

    /* --- the walk ----------------------------------------------------------------- */
    while (*p) {
        const wchar_t* end = wcschr(p, L'\\');
        size_t len = end ? (size_t)(end - p) : wcslen(p);

        if (len > REF_MAXCOMP) { out[0] = 0; return REF_EXCED; }

        if (len == 0) {                                 /* a separator emits itself */
            if (ol + 2 > usable) return REF_FULL();
            buf[ol++] = L'\\';
            ++p;
            continue;
        }
        if (len == 1 && p[0] == L'.') {                 /* "." */
            if (end) { p = end + 1; }                   /* skip it AND the separator after it */
            else {
                ++p;
                if (ol) { buf[ol] = 0; if (!ref_isroot(buf)) --ol; }
            }
            continue;
        }
        if (len == 2 && p[0] == L'.' && p[1] == L'.') {  /* ".." */
            buf[ol] = 0;
            if (ol == 0 || ref_isroot(buf)) {
                p = end ? end + 1 : p + len;            /* refused: skip it and its separator */
            } else {
                size_t c = ol - 1;                      /* the last character is not examined */
                for (;;) {
                    if (c == 0) { ol = 0; break; }
                    --c;
                    if (buf[c] == L'\\') { ol = c; break; }
                }
                p += 2;                                 /* the separator after ".." is NOT eaten */
            }
            continue;
        }
        if (ol + len + 1 > usable) return REF_FULL();
        memcpy(buf + ol, p, len * sizeof(wchar_t));
        ol += len;
        p += len;
    }

    /* --- the finish --------------------------------------------------------------- */
    while (ol > 0 && buf[ol-1] == L'.') {               /* trailing dots, with the '*' guard */
        if (ol >= 2 && buf[ol-2] == L'*') break;
        --ol;
    }
    /* Both remaining fixups are BEST-EFFORT: when the buffer cannot hold the extra character the
       function does NOT fail, it returns S_OK with the unfixed string. Measured: cch 1 on "" gives
       S_OK and "", and cch 3 on "C:" gives S_OK and "C:". */
    if (ol == 0) {
        if (usable > 1) buf[ol++] = L'\\';
    } else if (ol == 2 && buf[1] == L':') {
        if (usable > 3) buf[ol++] = L'\\';
    }
    buf[ol] = 0;

    memcpy(out, buf, (ol + 1) * sizeof(wchar_t));
    return REF_OK;
#undef REF_FULL
}
