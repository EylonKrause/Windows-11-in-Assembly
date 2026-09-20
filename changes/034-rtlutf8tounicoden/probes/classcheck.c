/* classcheck.c: independent three-way check of changes/034's TGL variant, per input class.
 *
 * bad32check.c established that the variant returns the right STATUS and the right byte count on
 * the bench's `bad32` class while leaving most of the destination UNWRITTEN, which is why that
 * row measured ~9.4 ns at every length. correctness.c passes 327,758 cases and cannot see it.
 *
 * The question this answers is whether the mixed-width classes, the whole point of the change --
 * are genuinely decoded or are the same illusion. It writes a known sentinel over the destination,
 * calls the live export and the variant on identical input, and compares the status, the byte
 * count AND every output byte.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern long wia_u82u(wchar_t*, unsigned long, unsigned long*, const char*, unsigned long);
typedef long (NTAPI *FN)(wchar_t*, ULONG, ULONG*, const char*, ULONG);

static void put2(unsigned char* s, int* i, unsigned cp) {       /* 2-byte sequence */
    s[(*i)++] = (unsigned char)(0xC0 | (cp >> 6));
    s[(*i)++] = (unsigned char)(0x80 | (cp & 0x3F));
}
static void put3(unsigned char* s, int* i, unsigned cp) {
    s[(*i)++] = (unsigned char)(0xE0 | (cp >> 12));
    s[(*i)++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    s[(*i)++] = (unsigned char)(0x80 | (cp & 0x3F));
}
static void put4(unsigned char* s, int* i, unsigned cp) {
    s[(*i)++] = (unsigned char)(0xF0 | (cp >> 18));
    s[(*i)++] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    s[(*i)++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    s[(*i)++] = (unsigned char)(0x80 | (cp & 0x3F));
}

/* n bytes in the named class, mirroring what bench_tgl.c's own fill() produces */
static int fill(const char* cls, unsigned char* s, int n) {
    int i = 0, k = 0;
    while (i < n) {
        int room = n - i;
        if      (!strcmp(cls, "ascii")) s[i++] = (unsigned char)('a' + (k & 15));
        else if (!strcmp(cls, "2byte")) { if (room < 2) break; put2(s, &i, 0x00E9); }
        else if (!strcmp(cls, "3byte")) { if (room < 3) break; put3(s, &i, 0x4E00 + (k & 255)); }
        else if (!strcmp(cls, "4byte")) { if (room < 4) break; put4(s, &i, 0x1F600 + (k & 63)); }
        else if (!strcmp(cls, "a+3")) {
            if ((k & 3) == 3) { if (room < 3) break; put3(s, &i, 0x20AC); }
            else s[i++] = (unsigned char)('a' + (k & 15));
        }
        else if (!strcmp(cls, "a+4")) {
            if ((k & 7) == 7) { if (room < 4) break; put4(s, &i, 0x1F600); }
            else s[i++] = (unsigned char)('a' + (k & 15));
        }
        else if (!strcmp(cls, "2+3")) {
            if (k & 1) { if (room < 3) break; put3(s, &i, 0x2010); }
            else       { if (room < 2) break; put2(s, &i, 0x03B1); }
        }
        else if (!strcmp(cls, "1234")) {
            int w = k & 3;
            if (w == 0) s[i++] = (unsigned char)('a' + (k & 15));
            else if (w == 1) { if (room < 2) break; put2(s, &i, 0x00E9); }
            else if (w == 2) { if (room < 3) break; put3(s, &i, 0x4E00); }
            else             { if (room < 4) break; put4(s, &i, 0x1F600); }
        }
        else if (!strcmp(cls, "bad32")) {
            s[i] = (i % 32 == 31) ? 0x80 : (unsigned char)('a' + (k & 15));
            ++i;
        }
        ++k;
    }
    return i;
}

int main(void) {
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    FN sys = (FN)GetProcAddress(h, "RtlUTF8ToUnicodeN");
    if (!sys) { printf("cannot resolve RtlUTF8ToUnicodeN\n"); return 2; }

    static unsigned char src[40000];
    static wchar_t a[40000], b[40000];
    static const char* CLS[] = { "ascii","2byte","3byte","4byte","a+3","a+4","2+3","1234","bad32" };
    static const int LENS[] = { 64, 512, 4000, 32000 };

    int bad = 0, total = 0;
    printf("%-7s %-7s  %-10s %-10s  %-10s %-10s  %s\n",
           "class","n","live st","live by","ours st","ours by","bytes");
    printf("--------------------------------------------------------------------------------\n");
    for (int c = 0; c < 9; ++c) {
        for (int k = 0; k < 4; ++k) {
            int n = fill(CLS[c], src, LENS[k]);
            ULONG ra = 0, rb = 0;
            memset(a, 0xAB, sizeof a);
            memset(b, 0xAB, sizeof b);
            long sa = sys(a, sizeof a, &ra, (const char*)src, (ULONG)n);
            long sb = wia_u82u(b, sizeof b, &rb, (const char*)src, (ULONG)n);
            int ok = (sa == sb) && (ra == rb) && memcmp(a, b, ra) == 0;
            ++total; if (!ok) ++bad;
            printf("%-7s %-7d  %08lX   %-10lu  %08lX   %-10lu  %s\n",
                   CLS[c], n, (unsigned long)sa, (unsigned long)ra,
                   (unsigned long)sb, (unsigned long)rb, ok ? "match" : "*** DIFFER ***");
            if (!ok && ra == rb) {
                for (ULONG i = 0; i < ra / 2; ++i)
                    if (a[i] != b[i]) {
                        printf("          first differing wchar %lu of %lu: live U+%04X ours U+%04X%s\n",
                               (unsigned long)i, (unsigned long)(ra / 2), a[i], b[i],
                               b[i] == 0xABAB ? "  (ours never wrote it)" : "");
                        break;
                    }
            }
        }
    }
    printf("--------------------------------------------------------------------------------\n");
    printf("%d of %d subjects differ from the live export\n", bad, total);
    return bad ? 1 : 0;
}
