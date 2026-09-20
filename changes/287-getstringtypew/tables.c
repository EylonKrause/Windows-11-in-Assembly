/* changes/287-getstringtypew/tables.c
 *
 * The two-level classification tables, built from the live export and re-checked against it.
 *
 * probes/contract.c established the three facts that make a table legal at all, and each of them would
 * have killed the change on its own:
 *
 *   * CONTEXT-FREEDOM -- 20000 random strings up to 2048 code units, 0 words where a character's class
 *     differed from the class it gets alone, for all three info types;
 *   * LOCALE INVARIANCE -- the whole CT_CTYPE1 table rebuilt under seven thread locales (en-US, de-DE,
 *     ru-RU, ja-JP, ko-KR, pt-BR, ar-SA), 0 entries different;
 *   * TOTALITY -- every one of the 65535 non-zero code units is classified; none is refused.
 *
 * probes/tableshape.c then measured how much of the table is redundant, because a flat 65536-WORD table
 * is 128 KB per info type and 384 KB for the three, which does not fit L2 and would make this a slow
 * implementation of a fast idea:
 *
 *              distinct values   distinct 256-entry pages   two-level size
 *   CT_CTYPE1        20                   62 of 256            32000 bytes   (4.1x smaller)
 *   CT_CTYPE2        12                   62 of 256            32000 bytes
 *   CT_CTYPE3        57                   67 of 256            34560 bytes   (3.8x smaller)
 *
 * So the layout is the classic two-level one: a 256-entry directory indexed by the HIGH byte giving a
 * page number, and the deduplicated pages indexed by the LOW byte. All three types together come to
 * about 98 KB rather than 384 KB, and the handful of pages that real text touches fit in L1.
 *
 * The tables are built at init from the live export, which is the same thing change 281 does with
 * wia_sci_init: derive, then re-check. 65536 single-character calls take a few milliseconds, and the
 * result is correct by construction rather than by a generated file that could drift. A real image
 * build would bake the finished tables in; here they are derived and verified every run, and
 * wia_gst_init returns non-zero if anything about the export has moved.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef BOOL (WINAPI *FGST)(DWORD, LPCWCH, int, LPWORD);

/* 3 info types; the page count is measured at 62/62/67, so 96 is generous and fixed. */
#define GST_MAXPAGE 96

unsigned char wia_gst_dir[3][256];              /* high byte -> page number */
unsigned short wia_gst_page[3][GST_MAXPAGE][256];  /* page, low byte -> class */
unsigned char wia_gst_npage[3];
unsigned short wia_gst_flat[3][256];            /* the U+0000..U+00FF page, hoisted for the fast path */

/* The flat table, and why it exists alongside the two-level one.
 *
 * The two-level layout is 4x smaller and was built first for exactly that reason. Then the bench
 * measured it: 180 ns for 511 code units, only 2.25x the shipped export, because every unit costs a
 * compare, a branch and two dependent loads. The saving was real and the speed was not.
 *
 * 384 KB of zero-initialised BSS buys one load and no branch per unit, and for ASCII text only the
 * first 512 bytes of each table are ever touched, so it is L1-resident in the case that matters. The
 * two-level tables are kept because the scalar MODEL uses them: the model and the implementation then
 * reach the same answer by genuinely different routes, which is what makes the three-way gate worth
 * running. tables.c proves they agree for all 65536 entries before either is used. */
unsigned short wia_gst_full[3][65536];

static const DWORD KIND[3] = { 1 /*CT_CTYPE1*/, 2 /*CT_CTYPE2*/, 4 /*CT_CTYPE3*/ };

int wia_gst_init(void)
{
    static int done = 0;
    static int result = 1;
    HMODULE kb, k32;
    FGST gst;
    int t;
    static unsigned short flat[65536];

    if (done) return result;
    done = 1;

    kb = LoadLibraryW(L"kernelbase.dll");
    k32 = LoadLibraryW(L"kernel32.dll");
    gst = kb ? (FGST)GetProcAddress(kb, "GetStringTypeW") : 0;
    if (!gst && k32) gst = (FGST)GetProcAddress(k32, "GetStringTypeW");
    if (!gst) return result;                    /* 1 = failure */

    for (t = 0; t < 3; ++t) {
        int c, p, np = 0;

        /* the flat table, one code unit at a time. U+0000 has a class too, and it is reachable only
           with an explicit count, since a NUL-terminated call would stop before it. */
        {
            WCHAR one[2];
            WORD out[2];
            for (c = 0; c < 65536; ++c) {
                one[0] = (WCHAR)c; one[1] = 0;
                out[0] = 0;
                if (!gst(KIND[t], one, 1, out)) return result;
                flat[c] = out[0];
            }
        }

        /* deduplicate the 256 pages */
        for (p = 0; p < 256; ++p) {
            int q, dup = -1;
            for (q = 0; q < np; ++q) {
                int k, same = 1;
                for (k = 0; k < 256; ++k)
                    if (wia_gst_page[t][q][k] != flat[p * 256 + k]) { same = 0; break; }
                if (same) { dup = q; break; }
            }
            if (dup < 0) {
                int k;
                if (np >= GST_MAXPAGE) return result;   /* the measured count moved: refuse */
                for (k = 0; k < 256; ++k) wia_gst_page[t][np][k] = flat[p * 256 + k];
                dup = np++;
            }
            wia_gst_dir[t][p] = (unsigned char)dup;
        }
        wia_gst_npage[t] = (unsigned char)np;

        /* the Latin-1 page, hoisted: this is the page real text lives in, and having it as its own
           flat array lets the implementation skip the directory load entirely for such a string */
        {
            int k;
            for (k = 0; k < 256; ++k) wia_gst_flat[t][k] = flat[k];
        }

        /* the flat table the implementation uses */
        for (c = 0; c < 65536; ++c) wia_gst_full[t][c] = flat[c];

        /* RE-CHECK: every code unit back through the two levels, against the flat extraction, and
           then a sample back through the live export in BULK -- because the bulk path is what the
           implementation replaces, and a per-character extraction that happened to disagree with it
           would otherwise go unnoticed. */
        for (c = 0; c < 65536; ++c) {
            if (wia_gst_page[t][wia_gst_dir[t][c >> 8]][c & 0xFF] != flat[c]) return result;
            if (wia_gst_full[t][c] != flat[c]) return result;
        }

        {
            static WCHAR s[512];
            static WORD out[512];
            int base;
            for (base = 0; base + 511 < 65536; base += 511) {
                int k;
                for (k = 0; k < 511; ++k) s[k] = (WCHAR)(base + k);
                s[511] = 0;
                if (!gst(KIND[t], s, 511, out)) return result;
                for (k = 0; k < 511; ++k)
                    if (out[k] != flat[base + k]) return result;
            }
        }
    }

    result = 0;
    return result;
}
