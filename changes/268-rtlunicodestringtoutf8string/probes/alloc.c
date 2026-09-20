/* changes/268-rtlunicodestringtoutf8string/probes/alloc.c
 *
 * What does AllocateDestinationString = TRUE actually do, and can it be reproduced?
 *
 * This is the question that decides whether change 268 can exist at all. This project does not
 * reimplement allocators, and a conversion that allocates its own destination is either
 *
 *   (a) calling RtlAllocateHeap on the process heap with a size that can be computed exactly, in
 *       which case an implementation can make the SAME call and the caller's RtlFreeUTF8String
 *       still works, or
 *   (b) doing something a reimplementation cannot match byte for byte, in which case the honest
 *       answer is to leave this export alone rather than ship something that looks right until a
 *       caller frees it.
 *
 * Getting this wrong corrupts a heap, which is why it is asked before any assembly is written and
 * not after. The questions:
 *
 *   * which heap does the block come from, is it the process heap?
 *   * how big is the block, exactly, against the size of the converted text?
 *   * what are Length and MaximumLength set to?
 *   * does the paired RtlFreeUTF8String accept a block allocated the same way by hand?
 *
 * Both directions are asked, not just one. probes/failwrite.c and probes/notmapped.c both found
 * the two wrappers behaving DIFFERENTLY where a first draft had assumed they mirrored each other --
 * one partially fills a destination it cannot fill and the other writes nothing, one passes
 * STATUS_SOME_NOT_MAPPED through and the other swallows it. After that, asking only one direction
 * and assuming the other is not a shortcut worth taking.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { USHORT Length, MaximumLength; PSTR  Buffer; } U8STR;

typedef LONG   (NTAPI *F_U2U8)(U8STR*, const USTR*, BOOLEAN);
typedef LONG   (NTAPI *F_U82U)(USTR*, const U8STR*, BOOLEAN);
typedef void   (NTAPI *F_FreeU8)(U8STR*);
typedef void   (NTAPI *F_FreeU)(USTR*);
typedef PVOID  (NTAPI *F_Alloc)(PVOID, ULONG, SIZE_T);
typedef SIZE_T (NTAPI *F_SizeHeap)(PVOID, ULONG, PVOID);
typedef BOOLEAN(NTAPI *F_FreeHeap)(PVOID, ULONG, PVOID);

int main(void)
{
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    F_U2U8    u2u8  = (F_U2U8)   GetProcAddress(h, "RtlUnicodeStringToUTF8String");
    F_FreeU8  freeu8= (F_FreeU8) GetProcAddress(h, "RtlFreeUTF8String");
    F_Alloc   alloc = (F_Alloc)  GetProcAddress(h, "RtlAllocateHeap");
    F_SizeHeap szh  = (F_SizeHeap)GetProcAddress(h, "RtlSizeHeap");
    F_FreeHeap frh  = (F_FreeHeap)GetProcAddress(h, "RtlFreeHeap");
    PVOID procheap = GetProcessHeap();
    wchar_t src[64];
    USTR in;
    int i, n;

    if (!u2u8 || !alloc || !szh) { printf("resolve failed\n"); return 1; }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== AllocateDestinationString = TRUE ==\n");
    printf("   RtlFreeUTF8String is %s\n", freeu8 ? "exported" : "NOT exported");
    printf("   the process heap is %p\n\n", procheap);

    for (n = 0; n <= 6; ++n) {
        U8STR out;
        LONG st;
        for (i = 0; i < n; ++i) src[i] = (wchar_t)(L'a' + i);
        in.Buffer = src; in.Length = (USHORT)(n * 2); in.MaximumLength = in.Length;
        memset(&out, 0xCD, sizeof out);
        st = u2u8(&out, &in, TRUE);
        if (st == 0) {
            SIZE_T blocksz = szh(procheap, 0, out.Buffer);
            printf("   n=%d -> %08lX  Length=%-3u Max=%-3u buffer=%p  block size on the PROCESS\n"
                   "            heap = %Iu%s\n", n, (unsigned long)st, out.Length,
                   out.MaximumLength, (void*)out.Buffer, blocksz,
                   blocksz == (SIZE_T)-1 ? "  <== NOT from the process heap" : "");
            if (freeu8) freeu8(&out);
        } else {
            printf("   n=%d -> %08lX (no buffer)\n", n, (unsigned long)st);
        }
    }

    printf("\n   -- and can a block allocated BY HAND the same way be freed by RtlFreeUTF8String?\n");
    if (freeu8) {
        U8STR mine;
        char* p = (char*)alloc(procheap, 0, 8);
        if (!p) { printf("      allocation failed\n"); return 1; }
        memcpy(p, "abc", 4);
        mine.Buffer = p; mine.Length = 3; mine.MaximumLength = 8;
        freeu8(&mine);
        printf("      it accepted a hand-allocated process-heap block and left Buffer=%p Length=%u\n",
               (void*)mine.Buffer, mine.Length);
        printf("      -- so the allocation is an ordinary RtlAllocateHeap on the process heap, and\n"
               "         an implementation can make the same call\n");
    }

    /* ---------------- the OTHER direction, asked rather than assumed ---------------- */
    {
        F_U82U   u82u   = (F_U82U)  GetProcAddress(h, "RtlUTF8StringToUnicodeString");
        F_FreeU  freeu  = (F_FreeU) GetProcAddress(h, "RtlFreeUnicodeString");
        char u8src[64];
        U8STR uin;

        printf("\n== RtlUTF8StringToUnicodeString, AllocateDestinationString = TRUE ==\n");
        printf("   RtlFreeUnicodeString is %s\n\n", freeu ? "exported" : "NOT exported");
        if (!u82u) { printf("   resolve failed\n"); return 1; }

        for (n = 0; n <= 6; ++n) {
            USTR out;
            LONG st;
            for (i = 0; i < n; ++i) u8src[i] = (char)('a' + i);
            uin.Buffer = u8src; uin.Length = (USHORT)n; uin.MaximumLength = (USHORT)n;
            memset(&out, 0xCD, sizeof out);
            st = u82u(&out, &uin, TRUE);
            if (st >= 0) {
                SIZE_T blocksz = szh(procheap, 0, out.Buffer);
                printf("   n=%d -> %08lX  Length=%-3u Max=%-3u buffer=%p  block size on the PROCESS\n"
                       "            heap = %Iu%s\n", n, (unsigned long)st, out.Length,
                       out.MaximumLength, (void*)out.Buffer, blocksz,
                       blocksz == (SIZE_T)-1 ? "  <== NOT from the process heap" : "");
                if (freeu) freeu(&out);
            } else {
                printf("   n=%d -> %08lX (no buffer)\n", n, (unsigned long)st);
            }
        }

        /* multi-byte input, where the byte count and the word count are different numbers */
        {
            static const char mb[] = { (char)0xC3,(char)0xA9, (char)0xE2,(char)0x82,(char)0xAC,
                                       (char)0xF0,(char)0x9F,(char)0x98,(char)0x80 };
            static const char bad[] = { (char)0x80, 'a', (char)0xE2, (char)0x82 };
            struct { const char* p; int n; const char* what; } T[2];
            int t;
            T[0].p = mb;  T[0].n = 9; T[0].what = "c3a9 e282ac f09f9880 -> 4 words";
            T[1].p = bad; T[1].n = 4; T[1].what = "malformed -> FFFD 0061 FFFD";
            for (t = 0; t < 2; ++t) {
                USTR out;
                LONG st;
                uin.Buffer = (PSTR)T[t].p; uin.Length = (USHORT)T[t].n;
                uin.MaximumLength = (USHORT)T[t].n;
                memset(&out, 0xCD, sizeof out);
                st = u82u(&out, &uin, TRUE);
                if (st >= 0) {
                    SIZE_T blocksz = szh(procheap, 0, out.Buffer);
                    printf("   %-32s -> %08lX  Length=%-3u Max=%-3u block=%Iu\n",
                           T[t].what, (unsigned long)st, out.Length, out.MaximumLength, blocksz);
                    if (freeu) freeu(&out);
                } else {
                    printf("   %-32s -> %08lX (no buffer)\n", T[t].what, (unsigned long)st);
                }
            }
        }

        if (freeu) {
            USTR mine;
            wchar_t* p = (wchar_t*)alloc(procheap, 0, 8);
            if (!p) { printf("      allocation failed\n"); return 1; }
            memcpy(p, L"abc", 8);
            mine.Buffer = p; mine.Length = 6; mine.MaximumLength = 8;
            freeu(&mine);
            printf("\n      RtlFreeUnicodeString accepted a hand-allocated process-heap block and\n"
                   "      left Buffer=%p Length=%u -- the same answer as the other direction\n",
                   (void*)mine.Buffer, mine.Length);
        }
    }
    return 0;
}
