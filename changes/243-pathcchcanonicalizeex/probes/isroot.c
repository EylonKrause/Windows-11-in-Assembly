/* changes/243-pathcchcanonicalizeex/probes/isroot.c
   The truth table of the ROOT PREDICATE that PathCchCanonicalizeEx consults, derived by calling the
   internal helper directly.

   Reading the disassembly (see RESULTS.md) showed that the whole function is one linear walk, and that
   exactly one decision in it is not visible from the outside: before a ".." pops, and before a trailing
   "." eats a character, the code calls an internal predicate on the OUTPUT BUILT SO FAR
   (kernelbase+0x2BAA0 on this build) and refuses to shorten it when the predicate says yes. That single
   bit is what decides whether "C:\.." keeps its drive and whether "\\srv\..\.." grows back to "\\".

   Rather than guess it, this calls it. Enumerating it directly is legitimate for a PROBE -- it is
   read-only introspection of a rule we then reproduce from first principles -- but the address is
   build-specific and MUST NOT appear in an implementation. The model reproduces the RULE, never the
   call: probes/model.c encodes the predicate as ordinary code and the enumeration there is what proves
   the encoding right.

   The exported PathCchIsRoot is measured beside it, because if the two agree everywhere then the rule
   has a documented name and nothing internal needs to be described at all.

   Nothing here creates, opens or stats any file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define HELPER_RVA 0x2BAA0

typedef int  (WINAPI *PRED)(const wchar_t*);
typedef BOOL (WINAPI *ISROOT)(PCWSTR);

static PRED   internal;
static ISROOT pathcchisroot;

/* what the model will claim the predicate is */
static int is_letter(wchar_t c){ return (c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z'); }
static int model_pred(const wchar_t* s)
{
    size_t n = wcslen(s);
    if (n == 0) return 0;
    if (s[0] == L'\\') {
        if (n == 1) return 1;                                  /* "\"            */
        if (n == 2 && s[1] == L'\\') return 1;                  /* "\\"           */
        return 0;
    }
    if (n == 3 && is_letter(s[0]) && s[1] == L':' && s[2] == L'\\') return 1;   /* "C:\" */
    return 0;
}

static long long cases, diff_model, diff_exported;
static int shown_m, shown_e;

static void one(const wchar_t* s)
{
    int a = internal(s) != 0;
    int b = pathcchisroot(s) != 0;
    int c = model_pred(s) != 0;
    ++cases;
    if (a != c) { ++diff_model;    if (shown_m < 30) { printf("    MODEL  \"%ls\" internal %d model %d\n", s, a, c); ++shown_m; } }
    if (a != b) { ++diff_exported; if (shown_e < 30) { printf("    EXPORT \"%ls\" internal %d PathCchIsRoot %d\n", s, a, b); ++shown_e; } }
}

static void enumerate(const wchar_t* alpha, int maxlen)
{
    int base = (int)wcslen(alpha);
    wchar_t buf[32];
    for (int len = 0; len <= maxlen; ++len) {
        long long total = 1;
        for (int i = 0; i < len; ++i) total *= base;
        for (long long v = 0; v < total; ++v) {
            long long x = v;
            for (int i = 0; i < len; ++i) { buf[i] = alpha[x % base]; x /= base; }
            buf[len] = 0;
            one(buf);
        }
    }
}

int main(void){
    setvbuf(stdout, 0, _IONBF, 0);
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    internal = (PRED)((char*)hk + HELPER_RVA);
    pathcchisroot = (ISROOT)GetProcAddress(hk, "PathCchIsRoot");
    if (!pathcchisroot) { printf("cannot resolve PathCchIsRoot\n"); return 1; }
    printf("kernelbase = %p, internal predicate = %p\n", (void*)hk, (void*)internal);

    printf("\n=== the cases the walk actually reaches ===\n");
    static const wchar_t* KEY[] = {
        L"", L"\\", L"\\\\", L"\\\\\\", L"\\\\\\\\", L"C:", L"C:\\", L"C:\\\\", L"C:\\a", L"C:\\a\\",
        L"a", L"a\\", L"a\\\\", L"\\a", L"\\a\\", L"\\\\a", L"\\\\a\\", L"\\\\srv", L"\\\\srv\\",
        L"\\\\srv\\shr", L"\\\\srv\\shr\\", L"\\\\?\\", L"\\\\?\\C:", L"\\\\?\\C:\\", L"\\\\.\\",
        L"c:\\", L"1:\\", L":\\", L"C:a", L"\\:", L"::", L".:", L"\\\\?\\UNC\\", L"\\\\?\\UNC\\s\\",
        L"\\\\?\\UNC\\s\\h", L"\\\\?\\UNC\\s\\h\\"
    };
    for (int i = 0; i < (int)(sizeof(KEY)/sizeof(KEY[0])); ++i)
        printf("   %-22ls internal %d   PathCchIsRoot %d   model %d\n",
               KEY[i], internal(KEY[i]), pathcchisroot(KEY[i]) ? 1 : 0, model_pred(KEY[i]));

    printf("\n=== enumerated ===\n");
    enumerate(L"\\.a:C", 5);
    enumerate(L"\\?UNCa:.", 4);
    printf("    %lld cases: %lld differ from the model, %lld differ from PathCchIsRoot\n",
           cases, diff_model, diff_exported);
    return 0;
}
