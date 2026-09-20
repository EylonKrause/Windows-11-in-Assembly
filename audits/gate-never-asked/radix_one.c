/* One call per process: radix1.exe <ours|ucrt> <fn> <radix>
 *
 * The all-in-one version died at exit code 148 on the very first call and printed nothing, which is
 * the signature of a FAIL-FAST; an uncatchable termination, not an exception __except can see.
 * Changes 274 and 276 hit the same wall calling SysFreeString on a hand-made BSTR. The only way to
 * attribute a fail-fast to one side is to give each call its own process and read the exit code.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef char*    (__cdecl *F_UL)(unsigned long, char*, int);
typedef char*    (__cdecl *F_I)(int, char*, int);
typedef char*    (__cdecl *F_U64)(unsigned __int64, char*, int);
typedef char*    (__cdecl *F_I64)(__int64, char*, int);
typedef wchar_t* (__cdecl *F_ULW)(unsigned long, wchar_t*, int);
typedef wchar_t* (__cdecl *F_IW)(int, wchar_t*, int);

extern char*    wia_ultoa(unsigned long, char*, int);
extern char*    wia_itoa(int, char*, int);
extern char*    wia_ui64toa(unsigned __int64, char*, int);
extern char*    wia_i64toa(__int64, char*, int);
extern wchar_t* wia_ultow(unsigned long, wchar_t*, int);
extern wchar_t* wia_itow(int, wchar_t*, int);

static void nop_iph(const wchar_t* a, const wchar_t* b, const wchar_t* c,
                    unsigned d, uintptr_t e)
{ (void)a;(void)b;(void)c;(void)d;(void)e; }

static char    b8[4096];
static wchar_t b16[4096];
#define POISON '#'

int main(int argc, char** argv)
{
    HMODULE h = LoadLibraryW(L"ucrtbase.dll");
    int ours, radix, i, n;
    const char* fn;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 4) { printf("usage: radix1 <ours|ucrt> <fn> <radix>\n"); return 2; }
    ours = strcmp(argv[1], "ours") == 0;
    fn = argv[2];
    radix = atoi(argv[3]);
    if (!h) { printf("no ucrtbase\n"); return 2; }
    _set_invalid_parameter_handler(nop_iph);

    memset(b8, POISON, sizeof b8);
    for (i = 0; i < 4096; ++i) b16[i] = 0x2323;

    if (strcmp(fn, "ultoa") == 0) {
        if (ours) wia_ultoa(3735928559ul, b8, radix);
        else ((F_UL)GetProcAddress(h, "_ultoa"))(3735928559ul, b8, radix);
    } else if (strcmp(fn, "itoa") == 0) {
        if (ours) wia_itoa(-12345, b8, radix);
        else ((F_I)GetProcAddress(h, "_itoa"))(-12345, b8, radix);
    } else if (strcmp(fn, "ui64toa") == 0) {
        if (ours) wia_ui64toa(1234567890123ull, b8, radix);
        else ((F_U64)GetProcAddress(h, "_ui64toa"))(1234567890123ull, b8, radix);
    } else if (strcmp(fn, "i64toa") == 0) {
        if (ours) wia_i64toa(-1234567890123ll, b8, radix);
        else ((F_I64)GetProcAddress(h, "_i64toa"))(-1234567890123ll, b8, radix);
    } else if (strcmp(fn, "ultow") == 0) {
        if (ours) wia_ultow(3735928559ul, b16, radix);
        else ((F_ULW)GetProcAddress(h, "_ultow"))(3735928559ul, b16, radix);
        n = 0; while (n < 80 && b16[n] != (wchar_t)0x2323) ++n;
        printf("OK n=%d \"", n);
        for (i = 0; i < n && i < 40; ++i) putchar(b16[i] ? (char)b16[i] : '.');
        printf("\" [");
        for (i = 0; i < 4; ++i) printf("%04X ", (unsigned short)b16[i]);
        printf("]\n");
        return 0;
    } else if (strcmp(fn, "itow") == 0) {
        if (ours) wia_itow(-12345, b16, radix);
        else ((F_IW)GetProcAddress(h, "_itow"))(-12345, b16, radix);
        n = 0; while (n < 80 && b16[n] != (wchar_t)0x2323) ++n;
        printf("OK n=%d \"", n);
        for (i = 0; i < n && i < 40; ++i) putchar(b16[i] ? (char)b16[i] : '.');
        printf("\" [");
        for (i = 0; i < 4; ++i) printf("%04X ", (unsigned short)b16[i]);
        printf("]\n");
        return 0;
    } else { printf("unknown fn\n"); return 2; }

    n = 0; while (n < 200 && b8[n] != POISON) ++n;
    printf("OK n=%d \"", n);
    for (i = 0; i < n && i < 40; ++i) putchar(b8[i] ? b8[i] : '.');
    printf("\" [");
    for (i = 0; i < 6; ++i) printf("%02X ", (unsigned char)b8[i]);
    printf("]\n");
    return 0;
}
