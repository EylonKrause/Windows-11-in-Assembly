/* changes/290-multibytetowidechar/probes/longbad.c
 *
 * An independent check of the one class this change's corpus cannot express.
 *
 * correctness.c drives 2646844 cases and every subject in it is at most 96 bytes. The TGL variant
 * adds a SIXTY-FOUR byte block, and a 64-byte block is a FULL block only when 64 or more source
 * bytes still remain -- so a defect in its full-block path needs a subject where a malformed byte
 * has a LONG remainder after it, which that corpus never builds.
 *
 * This is not hypothetical. Change 034's TGL variant had exactly that defect: it advanced both
 * cursors by the whole remaining source length instead of by 64, wrote the first 64 units
 * correctly, skipped the rest, and still returned the correct count because the count is derived
 * from the same cursor. It passed all 327758 of its own cases. See changes/034's RESULTS-tgl.md.
 *
 * So this asks the live kernelbase export and the variant the same question on long subjects, and
 * compares the return value, the last error and every output unit -- with the destination
 * pre-filled with a sentinel, so "never written" is distinguishable from "written correctly".
 *
 * BUILD (from the change directory, after dot-sourcing tools/vsenv.ps1):
 *   cl /nologo /O2 probes\longbad.c reference.c impl_tgl.obj /Fe:longbad.exe
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern int  wia_mbtwc(UINT, DWORD, const char*, int, wchar_t*, int);
extern void wia_mbtwc_set_fallback(void*);
typedef int (WINAPI *FN)(UINT, DWORD, LPCCH, int, LPWSTR, int);
static FN sys;

#define CAP 40000
static wchar_t a[CAP], b[CAP];
static unsigned char src[CAP];
static long checked = 0, bad = 0;

/* one subject, three comparisons; reports the first divergence and whether we wrote at all */
static int one(const unsigned char* s, int n, const char* what) {
    int ra, rb, i, lim;
    DWORD ea, eb;
    memset(a, 0xAB, sizeof a);
    memset(b, 0xAB, sizeof b);
    SetLastError(0xD1CE); ra = sys(CP_UTF8, 0, (LPCCH)s, n, a, CAP); ea = GetLastError();
    SetLastError(0xD1CE); rb = wia_mbtwc(CP_UTF8, 0, (const char*)s, n, b, CAP); eb = GetLastError();
    ++checked;
    if (ra != rb || ea != eb) {
        printf("  %-28s n=%-6d live ret=%-6d err=%-6lu | ours ret=%-6d err=%-6lu  *** DIFFER ***\n",
               what, n, ra, (unsigned long)ea, rb, (unsigned long)eb);
        ++bad; return 1;
    }
    lim = ra < rb ? ra : rb;
    for (i = 0; i < lim; ++i)
        if (a[i] != b[i]) {
            printf("  %-28s n=%-6d ret=%d: first differing unit %d of %d: live U+%04X ours U+%04X%s\n",
                   what, n, ra, i, ra, a[i], b[i],
                   b[i] == 0xABAB ? "   (OURS NEVER WROTE IT)" : "");
            ++bad; return 1;
        }
    return 0;
}

static int fill_class(int kind, unsigned char* s, int n) {
    int i = 0, k = 0;
    while (i < n) {
        int room = n - i;
        switch (kind) {
        case 0: s[i++] = (unsigned char)('a' + (k & 15)); break;
        case 1: if (room < 4) { s[i++] = 'z'; break; }                     /* a+3 */
                if ((k & 3) == 3) { s[i++]=0xE2; s[i++]=0x82; s[i++]=0xAC; }
                else s[i++] = (unsigned char)('a' + (k & 15));
                break;
        case 2: if (room < 5) { s[i++] = 'z'; break; }                     /* a+4 */
                if ((k & 7) == 7) { s[i++]=0xF0; s[i++]=0x9F; s[i++]=0x98; s[i++]=0x80; }
                else s[i++] = (unsigned char)('a' + (k & 15));
                break;
        case 3: if (room < 3) { s[i++] = 'z'; break; }                     /* 2+3 */
                if (k & 1) { s[i++]=0xE2; s[i++]=0x80; s[i++]=0x90; }
                else       { s[i++]=0xCE; s[i++]=0xB1; }
                break;
        default: if (room < 4) { s[i++] = 'z'; break; }                    /* 1234 */
                switch (k & 3) {
                case 0: s[i++] = (unsigned char)('a' + (k & 15)); break;
                case 1: s[i++]=0xC3; s[i++]=0xA9; break;
                case 2: s[i++]=0xE4; s[i++]=0xB8; s[i++]=0x80; break;
                default: s[i++]=0xF0; s[i++]=0x9F; s[i++]=0x98; s[i++]=0x80; break;
                }
                break;
        }
        ++k;
    }
    return i;
}

int main(void) {
    static const char* CN[5] = { "ascii", "a+3", "a+4", "2+3", "1234" };
    static const unsigned char SPOIL[7] = { 0x80, 0x9F, 0xBF, 0xC0, 0xC1, 0xF5, 0xFF };
    static const int LEN[6] = { 65, 128, 300, 512, 4096, 32000 };
    static const int PERIOD[5] = { 16, 32, 33, 64, 97 };
    int c, li, sp, pi, k, n, stop = 0;

    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    sys = (FN)GetProcAddress(h, "MultiByteToWideChar");
    if (!sys) { printf("cannot resolve MultiByteToWideChar\n"); return 2; }
    wia_mbtwc_set_fallback((void*)sys);

    printf("== well-formed long subjects ==\n");
    for (c = 0; c < 5 && !stop; ++c)
        for (li = 0; li < 6; ++li) { n = fill_class(c, src, LEN[li]); stop |= one(src, n, CN[c]); }

    printf("== one malformed byte at EVERY offset of a 300-byte subject ==\n");
    for (c = 0; c < 5 && !stop; ++c)
        for (sp = 0; sp < 7 && !stop; ++sp)
            for (k = 0; k < 300; ++k) {
                char tag[64];
                n = fill_class(c, src, 300);
                src[k] = SPOIL[sp];
                sprintf(tag, "%s +%02X@k", CN[c], SPOIL[sp]);
                if (one(src, n, tag)) { stop = 1; break; }
            }

    printf("== recurring malformed bytes, five periods, six lengths ==\n");
    for (c = 0; c < 5 && !stop; ++c)
        for (sp = 0; sp < 7 && !stop; ++sp)
            for (pi = 0; pi < 5 && !stop; ++pi)
                for (li = 0; li < 6; ++li) {
                    char tag[64];
                    n = fill_class(c, src, LEN[li]);
                    for (k = PERIOD[pi] - 1; k < n; k += PERIOD[pi]) src[k] = SPOIL[sp];
                    sprintf(tag, "%s +%02X/%d", CN[c], SPOIL[sp], PERIOD[pi]);
                    if (one(src, n, tag)) { stop = 1; break; }
                }

    printf("\n%ld subjects checked, %ld differ from the live export\n", checked, bad);
    printf("%s\n", bad ? "*** THE VARIANT DOES NOT MATCH kernelbase ***"
                       : "every subject matches kernelbase on the return value, the last error and every unit");
    return bad ? 1 : 0;
}
