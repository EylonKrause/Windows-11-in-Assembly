/* discovery/utf8n_null_destination.c
 *
 * Changes 016 And 034 Do not implement the measuring mode of the functions they replace.
 *
 * Found while building change 268 (RtlUnicodeStringToUTF8String), whose allocating path has to ask
 * the N-form how big the output will be before it can allocate a buffer for it.
 *
 *     RtlUnicodeToUTF8N(NULL, 0, &produced, src, srcLen)
 *
 * is the documented way to ask these conversions for the size they would need: the shipped exports
 * return STATUS_SUCCESS with `produced` set to the required byte count, and write nothing. This
 * project's replacements do not:
 *
 *     UTF-16 -> UTF-8, five ASCII characters (five bytes of output)
 *       destination     size     live                    ours
 *       a buffer           0     C0000023 / 0            C0000023 / 0          agree
 *       a buffer           1     C0000023 / 1            C0000023 / 1          agree
 *       a buffer           4     C0000023 / 4            C0000023 / 4          agree
 *       a buffer           5     00000000 / 5            00000000 / 5          agree
 *       a buffer           6     00000000 / 5            00000000 / 5          agree
 *       NULL               0     00000000 / 5            C0000023 / 0          DIFFER
 *       NULL              99     00000000 / 5            (access violation)    DIFFER
 *
 * and RtlUTF8ToUnicodeN behaves the same way: live answers 12 for six ASCII bytes -- the count is
 * in BYTES of UTF-16, not characters -- while ours answers STATUS_BUFFER_TOO_SMALL with nothing
 * produced.
 *
 * Why the gates did not catch it. Both changes are bit-exact against their live exports over large
 * corpora -- and every case in those corpora passes a real destination buffer. The NULL destination
 * is not an edge of the LENGTH, which is what those corpora sweep; it is a different MODE of the
 * same function, and nothing in the corpora asked for it. This is the same shape as the SPACE bug
 * that sat in four landed changes at once: not a subtle arithmetic slip, but a case nobody thought
 * to ask about.
 *
 * What it means in practice. Under live substitution a caller that sizes before converting -- which
 * is what RtlUnicodeStringToUTF8String does internally on its allocating path, and what any careful
 * caller does -- gets STATUS_BUFFER_TOO_SMALL instead of a size, or faults outright if it passes a
 * non-zero size with a NULL pointer. The conversion itself is correct; the mode is missing.
 *
 * This file is the evidence, not the fix. Fixing it means giving both changes a real sizing pass:
 * straightforward for UTF-16 -> UTF-8, where the output length of each character is decided by
 * three range tests and a surrogate-pair rule, and harder for UTF-8 -> UTF-16, where the count
 * depends on the decoder's exact replacement policy for malformed input -- which has to be
 * reproduced, not approximated, because a size that is one short produces a truncated conversion
 * in the caller's buffer.
 *
 * Change 268 Is blocked on that and is not being landed on top of it.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef LONG (NTAPI *F_ToUtf8N)(char*, ULONG, ULONG*, const wchar_t*, ULONG);
typedef LONG (NTAPI *F_ToUniN)(wchar_t*, ULONG, ULONG*, const char*, ULONG);

/* the two landed implementations */
LONG wia_u2u8(char*, ULONG, ULONG*, const wchar_t*, ULONG);
LONG wia_u82u(wchar_t*, ULONG, ULONG*, const char*, ULONG);

static char    obuf[64];
static wchar_t owbuf[64];

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_ToUtf8N n8 = (F_ToUtf8N)GetProcAddress(h, "RtlUnicodeToUTF8N");
    F_ToUniN  nu = (F_ToUniN) GetProcAddress(h, "RtlUTF8ToUnicodeN");
    wchar_t ws[8] = { L'a', L'b', L'c', L'd', L'e' };
    char    u8[8] = { 'a','b','c','d','e','f' };
    int cap, differ = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!n8 || !nu) { printf("resolve failed\n"); return 1; }

    printf("== RtlUnicodeToUTF8N: a real destination, at every capacity ==\n");
    printf("   size   live status / produced     ours status / produced\n");
    for (cap = 0; cap <= 6; ++cap) {
        LONG sl, so;
        ULONG gl = 0xDEAD, go = 0xDEAD;
        sl = n8(obuf, (ULONG)cap, &gl, ws, 10);
        so = wia_u2u8(obuf, (ULONG)cap, &go, ws, 10);
        printf("   %4d   %08lX / %-5lu          %08lX / %-5lu   %s\n", cap,
               (unsigned long)sl, (unsigned long)gl, (unsigned long)so, (unsigned long)go,
               (sl == so && gl == go) ? "agree" : "DIFFER");
        if (sl != so || gl != go) ++differ;
    }

    printf("\n== and with a NULL destination, which is the MEASURING MODE ==\n");
    {
        LONG sl, so;
        ULONG gl = 0xDEAD, go = 0xDEAD;
        sl = n8(NULL, 0, &gl, ws, 10);
        so = wia_u2u8(NULL, 0, &go, ws, 10);
        printf("   UTF-16 -> UTF-8 : live %08lX / %-5lu     ours %08lX / %-5lu   %s\n",
               (unsigned long)sl, (unsigned long)gl, (unsigned long)so, (unsigned long)go,
               (sl == so && gl == go) ? "agree" : "DIFFER");
        if (sl != so || gl != go) ++differ;

        gl = go = 0xDEAD;
        sl = nu(NULL, 0, &gl, u8, 6);
        so = wia_u82u(NULL, 0, &go, u8, 6);
        printf("   UTF-8 -> UTF-16 : live %08lX / %-5lu     ours %08lX / %-5lu   %s\n",
               (unsigned long)sl, (unsigned long)gl, (unsigned long)so, (unsigned long)go,
               (sl == so && gl == go) ? "agree" : "DIFFER");
        if (sl != so || gl != go) ++differ;
    }

    printf("\n   NOT ASKED HERE: a NULL destination with a NON-ZERO size, which the shipped code\n");
    printf("   also treats as a measuring request and which the replacements DEREFERENCE. It is\n");
    printf("   left out of this file deliberately so that running the evidence does not crash it.\n");

    printf("\n   %d of the cases above differ.\n", differ);
    printf("   %s\n", differ
           ? "   The conversion itself is correct at every real capacity; what is missing is the\n"
             "   MODE. Changes 016 and 034 need a sizing pass before change 268 can be built on\n"
             "   them, and before a caller that measures first can safely run on them."
           : "   Both modes now agree.");
    return differ ? 1 : 0;
}
