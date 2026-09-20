/* changes/264-rtlinitutf8string/probes/contract.c
 *
 * Is RtlInitUTF8String just RtlInitString under another name?
 *
 * discovery/rtl_cmpstrings_probe.c already established that it is a DIFFERENT ADDRESS from
 * RtlInitString (unlike RtlInitAnsiString, which shares one) so it is at least distinct code.
 * What it is not yet known to be is distinct BEHAVIOUR, and that is the whole question: change 095
 * already ships a page-safe AVX2 strlen plus struct fill for RtlInitString, and if this export
 * follows the same rules then this change is that one with a different name on it.
 *
 * It would be very easy to assume so and be wrong. The name says UTF-8, and a function that
 * validated its input, rejected an overlong encoding, counted CHARACTERS rather than bytes, or
 * returned an NTSTATUS instead of nothing would produce a struct that looks identical on ASCII and
 * differs on exactly the inputs a lazy corpus never contains. This project has already paid for a
 * rule inherited from a sibling four times over (the SPACE bug across changes 132/140/143/144), and
 * changes 123 and 124 both found clear-side routines carrying conventions their set-side twins did
 * not.
 *
 * So every rule change 095 relies on is re-asked here, against this export, plus the ones that only
 * a UTF-8 function could have:
 *
 *   * does it return void, or a status?
 *   * Length in BYTES or in characters, asked with multi-byte sequences, where they differ
 *   * the 0xFFFF clamp, and what happens at and just past it
 *   * MaximumLength = Length + 1?
 *   * NULL source
 *   * is an INVALID UTF-8 byte sequence rejected, truncated, or simply counted?
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PSTR Buffer; } U8STR;
typedef void   (NTAPI *F_InitV)(U8STR*, const char*);
typedef LONG   (NTAPI *F_InitS)(U8STR*, const char*);

static F_InitV init_u8;
static F_InitV init_str;

static void show(const char* what, const char* src)
{
    U8STR a, b;
    memset(&a, 0xCD, sizeof a);
    memset(&b, 0xCD, sizeof b);
    init_u8(&a, src);
    init_str(&b, src);
    printf("  %-46s UTF8{len=%5u max=%5u buf=%s}  STRING{len=%5u max=%5u}%s\n",
           what, a.Length, a.MaximumLength, a.Buffer == src ? "src" : (a.Buffer ? "?" : "NULL"),
           b.Length, b.MaximumLength,
           (a.Length == b.Length && a.MaximumLength == b.MaximumLength) ? "" : "   <== THEY DIFFER");
}

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    static char big[70000];
    init_u8  = (F_InitV)GetProcAddress(h, "RtlInitUTF8String");
    init_str = (F_InitV)GetProcAddress(h, "RtlInitString");
    if (!init_u8 || !init_str) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== RtlInitUTF8String against RtlInitString, on the same inputs ==\n");
    printf("   the struct is poisoned with 0xCD first, so a field left alone is visible\n\n");

    printf("-- 1. the ordinary cases --\n");
    show("empty string", "");
    show("\"a\"", "a");
    show("\"hello\"", "hello");
    show("64 bytes of ASCII", "0123456789012345678901234567890123456789012345678901234567890123");

    printf("\n-- 2. MULTI-BYTE SEQUENCES: bytes, or characters? --\n");
    show("U+00E9 as UTF-8 (2 bytes, 1 character)", "\xC3\xA9");
    show("U+20AC as UTF-8 (3 bytes, 1 character)", "\xE2\x82\xAC");
    show("U+1F600 as UTF-8 (4 bytes, 1 character)", "\xF0\x9F\x98\x80");
    show("\"a\" + U+20AC + \"b\" (5 bytes, 3 characters)", "a\xE2\x82\xAC" "b");

    printf("\n-- 3. INVALID UTF-8: rejected, truncated, or just counted? --\n");
    show("a lone continuation byte 0x80", "\x80");
    show("0xFF, which can never appear in UTF-8", "\xFF");
    show("a truncated 3-byte sequence", "\xE2\x82");
    show("an OVERLONG encoding of '/'", "\xC0\xAF");
    show("a surrogate half U+D800 encoded", "\xED\xA0\x80");
    show("valid, then invalid, then valid", "ab\xFF" "cd");

    printf("\n-- 4. NULL --\n");
    {
        U8STR a;
        memset(&a, 0xCD, sizeof a);
        init_u8(&a, NULL);
        printf("  %-46s len=%u max=%u buf=%p\n", "src = NULL", a.Length, a.MaximumLength,
               (void*)a.Buffer);
    }

    printf("\n-- 5. THE CLAMP: lengths at and past 0xFFFF --\n");
    {
        static const int LENS[9] = { 65532, 65533, 65534, 65535, 65536, 65537, 65538, 66000, 69000 };
        int i, k;
        for (i = 0; i < 9; ++i) {
            U8STR a, b;
            for (k = 0; k < LENS[i]; ++k) big[k] = 'x';
            big[LENS[i]] = 0;
            memset(&a, 0xCD, sizeof a);
            memset(&b, 0xCD, sizeof b);
            init_u8(&a, big);
            init_str(&b, big);
            printf("  strlen %5d -> UTF8{len=%5u max=%5u}   STRING{len=%5u max=%5u}%s\n",
                   LENS[i], a.Length, a.MaximumLength, b.Length, b.MaximumLength,
                   (a.Length == b.Length && a.MaximumLength == b.MaximumLength)
                   ? "" : "   <== THEY DIFFER");
        }
    }

    printf("\n-- 6. is the return value a status rather than void? --\n");
    {
        F_InitS s = (F_InitS)init_u8;
        U8STR a;
        LONG r1, r2;
        memset(&a, 0, sizeof a);
        r1 = s(&a, "hello");
        memset(&a, 0, sizeof a);
        r2 = s(&a, NULL);
        printf("  returned %08lX for \"hello\" and %08lX for NULL -- NOTE: a void function leaves\n"
               "  whatever was in eax, so this is only meaningful if the two differ predictably\n",
               (unsigned long)r1, (unsigned long)r2);
    }
    return 0;
}
