/* changes/238-pathmakeprettya/reference.c
   An independent scalar oracle for shlwapi!PathMakePrettyA.

   Written the slow, obvious way -- two separate character loops and two table lookups -- so that it
   shares no structure with impl.asm. Every rule and both tables were derived from the narrow export
   in probes/pmpa2.c and probes/pmpa3.c, never from CharLowerA, never from the wide form, and never
   from a CP1252 table. The two counts are closed forms (60 and 34), which is how we know the tables
   are complete rather than approximate. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

static unsigned char LOW[256], UPP[256];
static int built = 0;
static void build(void)
{
    int v;
    for (v = 0; v < 256; ++v) { LOW[v] = (unsigned char)v; UPP[v] = (unsigned char)v; }

    /* LOWERCASE, applied from index 1 on: 26 + 23 + 7 + 3 + 1 = 60 values */
    for (v = 0x41; v <= 0x5A; ++v) LOW[v] = (unsigned char)(v + 0x20);
    for (v = 0xC0; v <= 0xD6; ++v) LOW[v] = (unsigned char)(v + 0x20);
    for (v = 0xD8; v <= 0xDE; ++v) LOW[v] = (unsigned char)(v + 0x20);
    LOW[0x8A] = 0x9A; LOW[0x8C] = 0x9C; LOW[0x8E] = 0x9E;
    LOW[0x9F] = 0xFF;

    /* UPPERCASE, applied to index 0: 23 + 7 + 3 + 1 = 34 OBSERVABLE values. 'a'..'z' are included
       for completeness but are unreachable -- any lowercase letter anywhere forces the refusal
       below, so index 0 can never hold one by the time this runs. */
    for (v = 0x61; v <= 0x7A; ++v) UPP[v] = (unsigned char)(v - 0x20);
    for (v = 0xE0; v <= 0xF6; ++v) UPP[v] = (unsigned char)(v - 0x20);
    for (v = 0xF8; v <= 0xFE; ++v) UPP[v] = (unsigned char)(v - 0x20);
    UPP[0x9A] = 0x8A; UPP[0x9C] = 0x8C; UPP[0x9E] = 0x8E;
    UPP[0xFF] = 0x9F;

    /* NOTE: neither table contains 0x5E or 0x88. Change 236's PathCommonPrefixA comparison fold
       conflates that pair, and it is NOT a case pair. Two functions in the same DLL, two different
       mappings -- which is why these were re-derived rather than reused. */
    built = 1;
}

int ref_pathmakeprettya(char* p)
{
    int i, len;
    if (!built) build();
    if (!p) return 0;

    /* The refusal scan is UNBOUNDED: a lowercase letter at index 560 of 600 still vetoes, and the
       refusal writes nothing at all. EXACTLY the 26 ASCII lowercase letters veto -- not the CP1252
       lowercase range, so 0xE0 does not. */
    for (i = 0; p[i]; ++i)
        if ((unsigned char)p[i] >= 0x61 && (unsigned char)p[i] <= 0x7A) return 0;
    len = i;

    if (len) {
        unsigned char first = (unsigned char)p[0];
        /* The rewrite is bounded at 259 characters, indices 0..258, which with a terminator is
           MAX_PATH -- and the bound is a TRUNCATION, not merely a stopping point: a longer path has
           a NUL written AT INDEX 259. probes/pmpa3.c reported index 259 as "left alone" because it
           tested whether the byte had been lowercased, and a not-lowercased test cannot tell
           "unchanged" from "replaced by a terminator". At len exactly 259 the write lands on the
           existing terminator and is invisible, which is why the guard is >= and not >. */
        int end = len < 259 ? len : 259;
        for (i = 1; i < end; ++i) p[i] = (char)LOW[(unsigned char)p[i]];
        if (len >= 259) p[259] = 0;
        p[0] = (char)UPP[first];
    }
    return 1;
}
