/* changes/315-strcspnia/tables.c
 * The match relation of StrCSpnIA, read from StrChrIA -- which StrCSpnIA calls once per character.
 *
 * kernelbase!StrCSpnIA(s, set) walks s and stops at the first character c for which StrChrIA(set, c)
 * finds something; StrChrIA compares by CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE |
 * LOCALE_USE_CP_ACP) per character. discovery/strchria_family.c: on a single-byte code page that is a
 * fixed, symmetric relation; on 1252 every needle matches at most two bytes (change 314).
 *
 * Two tables:
 *   wia_scs_memb[n][2]   the one or two bytes needle n matches (for the first character, which is
 *                        searched for in the set exactly as StrChrIA would, stopping at a match);
 *   wia_scs_bits[b][32]  the same set as a 256-bit bitmap in the layout impl.asm's vpshufb test reads:
 *                        byte lo (0..15) has bit h set when byte (h << 4 | lo) is a member, for h 0..7;
 *                        byte 16 + lo the same for h 8..15. A set's bitmap is the OR of its bytes' rows,
 *                        which is exact because the relation is symmetric (c matches set byte b iff b's
 *                        row contains c).
 * init refuses the fast path -- wia_scs_fb, every call handed to the export -- on a DBCS code page, when
 * the relation is not symmetric, when a needle matches more than two bytes or does not match itself,
 * or without AVX2/BMI1/BMI2.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <immintrin.h>

unsigned char wia_scs_memb[256][2];
__declspec(align(32)) unsigned char wia_scs_bits[256][32];
int wia_scs_fb = 1;
void* wia_scs_fb_cspn;

typedef char* (WINAPI *PCHR)(const char*, WORD);

static int cpu_ok(void) {
    int r[4];
    __cpuid(r, 1);
    if (!(r[2] & (1 << 27)) || !(r[2] & (1 << 28))) return 0;
    if ((_xgetbv(0) & 6) != 6) return 0;
    __cpuidex(r, 7, 0);
    return (r[1] & (1 << 5)) && (r[1] & (1 << 8)) && (r[1] & (1 << 3));    /* AVX2, BMI2, BMI1 */
}

static unsigned char rel[256][256];

int wia_scs_init(void) {
    HMODULE s = LoadLibraryW(L"shlwapi.dll");
    PCHR chr = (PCHR)GetProcAddress(s, "StrChrIA");
    wia_scs_fb_cspn = (void*)GetProcAddress(s, "StrCSpnIA");
    if (!chr || !wia_scs_fb_cspn) return 0;
    wia_scs_fb = 1;
    CPINFO ci;
    if (!cpu_ok() || !GetCPInfo(CP_ACP, &ci) || ci.MaxCharSize != 1) return 1;
    for (int n = 1; n < 256; ++n)
        for (int b = 1; b < 256; ++b) { char h[2] = { (char)b, 0 }; rel[n][b] = chr(h, (WORD)n) != NULL; }
    for (int n = 1; n < 256; ++n) {
        int cnt = 0, m[2] = { n, n };
        if (!rel[n][n]) return 1;
        for (int b = 1; b < 256; ++b) {
            if (rel[n][b] != rel[b][n]) return 1;
            if (rel[n][b]) { if (cnt < 2) m[cnt] = b; ++cnt; }
        }
        if (cnt > 2) return 1;
        if (cnt < 2) m[1] = m[0];
        wia_scs_memb[n][0] = (unsigned char)m[0]; wia_scs_memb[n][1] = (unsigned char)m[1];
        for (int b = 1; b < 256; ++b)
            if (rel[n][b]) wia_scs_bits[n][(b >> 7) * 16 + (b & 15)] |= (unsigned char)(1 << ((b >> 4) & 7));
    }
    wia_scs_fb = 0;
    return 1;
}
