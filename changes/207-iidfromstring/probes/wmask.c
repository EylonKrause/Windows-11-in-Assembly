/* changes/207-iidfromstring/probes/wmask.c
   Measure combase!IIDFromString's PARTIAL-WRITE behaviour exactly, position by position.

   The disassembly (RVA 0x000E6C20) shows the inner parser writing into the caller's GUID as it
   goes, with a granularity that differs per field: Data1 is pre-zeroed and re-stored after EVERY
   accepted digit, while Data2/Data3/Data4[n] are each stored only once the field and its trailing
   separator have validated. Rather than decode every store from 200 lines of assembly, this probe
   asks the live function directly: corrupt exactly one character, then report the HRESULT and which
   of the sixteen output bytes moved away from a poison fill.

   The result is the complete write-mask table, which the implementation must reproduce.           */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *FN)(const wchar_t*, GUID*);
static FN f;

#define POISON 0x5A

static const wchar_t GOOD[] = L"{DEADBEEF-1234-5678-9ABC-DEF011223344}";

/* returns a 16-bit mask of which output bytes were written, and the hresult */
static unsigned probe_one(const wchar_t* s, HRESULT* hr){
    GUID g; memset(&g, POISON, sizeof g);
    *hr = f(s, &g);
    const unsigned char* p = (const unsigned char*)&g;
    unsigned m = 0;
    for (int i = 0; i < 16; ++i) if (p[i] != POISON) m |= (1u << i);
    return m;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"combase.dll");
    f = (FN)GetProcAddress(h, "IIDFromString");
    if (!f) { h = LoadLibraryW(L"ole32.dll"); f = (FN)GetProcAddress(h, "IIDFromString"); }
    if (!f) { printf("no IIDFromString\n"); return 1; }

    printf("Baseline: the valid string.\n");
    {
        HRESULT hr; unsigned m = probe_one(GOOD, &hr);
        printf("  hr=%08lX  written mask=%04X (expect FFFF)\n\n", (unsigned long)hr, m);
    }

    printf("One character replaced by 'Z' (never valid anywhere), position by position.\n");
    printf("pos  char  hr        written-mask  bytes written\n");
    for (int pos = 0; pos < 38; ++pos) {
        wchar_t s[64];
        memcpy(s, GOOD, sizeof(GOOD));
        wchar_t orig = s[pos];
        s[pos] = L'Z';
        HRESULT hr; unsigned m = probe_one(s, &hr);
        printf("%3d   '%lc'  %08lX  %04X          ", pos, orig, (unsigned long)hr, m);
        if (m == 0) printf("(none)");
        else for (int i = 0; i < 16; ++i) if (m & (1u<<i)) printf("%d ", i);
        printf("\n");
    }

    printf("\nSame sweep but the replacement is '0' (a VALID hex digit) -- this isolates the\n"
           "positions where the character must be a separator or a brace rather than a digit.\n");
    printf("pos  char  hr        written-mask\n");
    for (int pos = 0; pos < 38; ++pos) {
        wchar_t s[64];
        memcpy(s, GOOD, sizeof(GOOD));
        wchar_t orig = s[pos];
        if (orig == L'0') continue;
        s[pos] = L'0';
        HRESULT hr; unsigned m = probe_one(s, &hr);
        if (hr != 0) printf("%3d   '%lc'  %08lX  %04X\n", pos, orig, (unsigned long)hr, m);
    }
    printf("(only the failing positions are listed; everything else accepted '0')\n");

    printf("\nData1's partial value: corrupt Data1's digit k and read back Data1.\n");
    printf("  k   string                                    hr        Data1\n");
    for (int k = 1; k <= 8; ++k) {
        wchar_t s[64];
        memcpy(s, GOOD, sizeof(GOOD));
        s[k] = L'Z';
        GUID g; memset(&g, POISON, sizeof g);
        HRESULT hr = f(s, &g);
        printf("  %d   %ls  %08lX  %08lX\n", k, s, (unsigned long)hr, (unsigned long)g.Data1);
    }

    printf("\nAnd the same for Data2 (chars 10..13) and Data4[7] (chars 35..36), to confirm those\n"
           "fields are all-or-nothing rather than progressive.\n");
    for (int k = 10; k <= 13; ++k) {
        wchar_t s[64];
        memcpy(s, GOOD, sizeof(GOOD));
        s[k] = L'Z';
        GUID g; memset(&g, POISON, sizeof g);
        HRESULT hr = f(s, &g);
        printf("  Data2 digit at %d: hr=%08lX  Data2=%04X  (poison would be 5A5A)\n",
               k, (unsigned long)hr, g.Data2);
    }
    for (int k = 35; k <= 36; ++k) {
        wchar_t s[64];
        memcpy(s, GOOD, sizeof(GOOD));
        s[k] = L'Z';
        GUID g; memset(&g, POISON, sizeof g);
        HRESULT hr = f(s, &g);
        printf("  Data4[7] digit at %d: hr=%08lX  Data4[7]=%02X  (poison would be 5A)\n",
               k, (unsigned long)hr, g.Data4[7]);
    }
    return 0;
}
