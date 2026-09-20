/* changes/274-sysallocstring/probes/contract.c
 *
 * What is a BSTR, exactly, and can one be made by hand?
 *
 * discovery/sid_inet_bstr.c measured the family:
 *
 *     oleaut32!SysStringLen        2.49 ns    should be a header read
 *     oleaut32!SysStringByteLen    2.31 ns
 *     oleaut32!SysAllocString    838.28 ns    scan + allocate + copy, on 8000 bytes
 *     oleaut32!SysAllocStringLen  65.65 ns    counted: no scan
 *     oleaut32!VarBstrCmp       3162.50 ns    takes an LCID, linguistic?
 *
 * The 838 ns is the interesting one and the gap to SysAllocStringLen says why: 773 ns of it is the
 * LENGTH SCAN, on a string whose length the caller was never asked for. A wcslen this project
 * already owns (change 001) runs at roughly 0.03 ns per character, so a 4000-character scan should
 * cost about 120 ns, not 773.
 *
 * But the allocation is the question that decides whether this change can exist at all. a BSTR is
 * documented as "a length prefix, the characters, and a terminator", allocated by oleaut32's own
 * allocator, and the caller frees it with SysFreeString. If that allocator is private, an
 * implementation cannot make a block SysFreeString will accept, and the most this change could be
 * is a faster scan feeding the real SysAllocStringLen. That is the same question change 268 had to
 * answer for RtlFreeUTF8String and change 269 for LocalFree, and both times the answer decided the
 * shape of the file.
 *
 * THE QUESTIONS:
 *
 *   1. Where is the length? Four bytes before the pointer, or somewhere else? Is it characters or
 *      bytes? What does SysStringLen read?
 *   2. Is the block one the caller could have made? Does SysFreeString accept a block this test
 *      builds by hand, and with which allocator?
 *   3. What is the contract at the edges: NULL, the empty string, a string with embedded NULs, and
 *      the largest string that works.
 *   4. Is SysAllocString really SysAllocStringLen after a wcslen? Compared directly, byte for byte,
 *      over many lengths.
 *   5. What does it do when the allocation fails, and is that reachable at all?
 *
 * Nothing is asserted. Every line prints what the live exports returned.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "oleaut32.lib")

static void dump_header(const char* what, BSTR b)
{
    printf("  %-26s ", what);
    if (!b) { printf("NULL\n"); return; }
    {
        unsigned char* raw = (unsigned char*)b;
        unsigned* pre = (unsigned*)(raw - 4);
        printf("ptr %p  [-4]=%u  SysStringLen=%u  SysStringByteLen=%u  wcslen=%u\n",
               (void*)b, pre[0], (unsigned)SysStringLen(b), (unsigned)SysStringByteLen(b),
               (unsigned)lstrlenW(b));
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("== 1. where is the length, and in what units? ==\n");
    {
        BSTR a = SysAllocString(L"hello");
        BSTR b = SysAllocString(L"");
        BSTR c = SysAllocStringLen(L"abcdef", 6);
        dump_header("SysAllocString(\"hello\")", a);
        dump_header("SysAllocString(\"\")", b);
        dump_header("SysAllocStringLen(6)", c);
        if (a) {
            unsigned char* raw = (unsigned char*)a;
            int i;
            printf("     the sixteen bytes from [-4]: ");
            for (i = -4; i < 12; ++i) printf("%02X ", raw[i]);
            printf("\n");
        }
        SysFreeString(a); SysFreeString(b); SysFreeString(c);
    }

    printf("\n== 2. is the terminator there, and is it counted? ==\n");
    {
        BSTR a = SysAllocString(L"abc");
        if (a) {
            printf("     characters: %04X %04X %04X %04X   (the fourth should be the terminator)\n",
                   a[0], a[1], a[2], a[3]);
            printf("     SysStringLen %u, byte length %u, prefix %u\n",
                   (unsigned)SysStringLen(a), (unsigned)SysStringByteLen(a),
                   ((unsigned*)((char*)a - 4))[0]);
            SysFreeString(a);
        }
    }

    printf("\n== 3. embedded NULs: a BSTR is counted, not terminated ==\n");
    {
        static const wchar_t src[7] = { 'a', 'b', 0, 'c', 'd', 0, 'e' };
        BSTR a = SysAllocStringLen(src, 7);
        BSTR b = SysAllocString(src);
        printf("     SysAllocStringLen(src,7) -> len %u\n", (unsigned)SysStringLen(a));
        printf("     SysAllocString(src)      -> len %u   (it stops at the first NUL)\n",
               (unsigned)SysStringLen(b));
        SysFreeString(a); SysFreeString(b);
    }

    printf("\n== 4. WHICH ALLOCATOR? can a block made by hand be freed by SysFreeString? ==\n");
    {
        /* The documented allocator for BSTRs is the OLE task allocator; the prefix sits four bytes
           before the pointer the caller sees, so a hand-made block has to allocate len*2+6 and
           return base+4. */
        const wchar_t* text = L"handmade";
        unsigned n = 8;
        unsigned char* base = (unsigned char*)CoTaskMemAlloc(n * 2 + 6);
        int ok = 0;
        if (base) {
            BSTR b;
            *(unsigned*)base = n * 2;
            memcpy(base + 4, text, n * 2);
            *(wchar_t*)(base + 4 + n * 2) = 0;
            b = (BSTR)(base + 4);
            printf("     hand-made through CoTaskMemAlloc: SysStringLen=%u, text \"%ls\"\n",
                   (unsigned)SysStringLen(b), b);
            /* SysFreeString on this block terminates the process. It is not an exception a
               __try/__except can catch -- the first version of this probe tried, and the run ended
               with exit code 116 partway through section 4. oleaut32 keeps its own cached allocator
               and a foreign block trips a fail-fast rather than raising.
               THAT IS THE ANSWER THIS SECTION EXISTS FOR: a BSTR block CANNOT BE MADE BY HAND, so
               this change cannot allocate one. It is left as a comment rather than as code, because
               running it proves the same thing and kills the probe. */
            CoTaskMemFree(base);
            ok = 1;
            printf("     SysFreeString on it: NOT ASKED -- it fail-fasts, which is the answer\n");
            (void)ok;
        }
    }
    {
        /* and through the heap, which it should NOT accept */
        const wchar_t* text = L"heapmade";
        unsigned n = 8;
        unsigned char* base = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, n * 2 + 6);
        if (base) {
            BSTR b;
            *(unsigned*)base = n * 2;
            memcpy(base + 4, text, n * 2);
            *(wchar_t*)(base + 4 + n * 2) = 0;
            b = (BSTR)(base + 4);
            printf("     hand-made through HeapAlloc:      SysStringLen=%u\n",
                   (unsigned)SysStringLen(b));
            printf("     (not freed through SysFreeString -- that would be the crash, not the test)\n");
            HeapFree(GetProcessHeap(), 0, base);
        }
    }

    printf("\n== 5. the real block is NOT a task-allocator block either ==\n");
    printf("     CoTaskMemRealloc on a real BSTR's base pointer FAIL-FASTS too -- the second\n"
           "     version of this probe asked and died at exit code 116 here. Between that and\n"
           "     section 4, the allocator behind SysAllocString is private to oleaut32 (it keeps a\n"
           "     cache of free BSTR blocks), and NO implementation outside it can produce a block\n"
           "     SysFreeString will accept.\n");
    printf("     THAT DECIDES THE SHAPE OF THIS CHANGE: the allocation must be CALLED, exactly as\n"
           "     change 269 calls LocalAlloc and change 272 calls MultiByteToWideChar. What is left\n"
           "     to own is the LENGTH SCAN -- which is where the time is.\n");
    {
        BSTR a = SysAllocString(L"who allocated me");
        unsigned char* base;
        if (a) {
            base = (unsigned char*)a - 4;
            printf("     for the record, the block's base is %p and its prefix says %u bytes\n",
                   (void*)base, *(unsigned*)base);
            SysFreeString(a);
        }
    }

    printf("\n== 6. the edges ==\n");
    {
        BSTR a = SysAllocString(0);
        printf("     SysAllocString(NULL) -> %p\n", (void*)a);
        if (a) SysFreeString(a);
        printf("     SysStringLen(NULL)   -> %u\n", (unsigned)SysStringLen(0));
        printf("     SysStringByteLen(NULL) -> %u\n", (unsigned)SysStringByteLen(0));
        printf("     SysFreeString(NULL)  -> ");
        SysFreeString(0);
        printf("returned\n");
    }

    printf("\n== 7. is SysAllocString exactly SysAllocStringLen after a wcslen? ==\n");
    {
        static wchar_t buf[8200];
        static const int LENS[] = { 0, 1, 2, 3, 7, 8, 15, 16, 31, 32, 100, 1000, 4000, 8000 };
        unsigned i;
        int bad = 0;
        for (i = 0; i < sizeof LENS / sizeof LENS[0]; ++i) {
            int n = LENS[i], k;
            BSTR a, b;
            for (k = 0; k < n; ++k) buf[k] = (wchar_t)(L'a' + (k % 26));
            buf[n] = 0;
            a = SysAllocString(buf);
            b = SysAllocStringLen(buf, (UINT)n);
            if (SysStringLen(a) != SysStringLen(b) ||
                SysStringByteLen(a) != SysStringByteLen(b) ||
                memcmp(a, b, (size_t)n * 2 + 2) != 0) {
                printf("     len %5d: THEY DIFFER (%u vs %u)\n", n,
                       (unsigned)SysStringLen(a), (unsigned)SysStringLen(b));
                ++bad;
            }
            SysFreeString(a); SysFreeString(b);
        }
        printf("     %d difference(s) over %d lengths\n", bad, (int)(sizeof LENS / sizeof LENS[0]));
    }

    printf("\n== 8. how big can one be? ==\n");
    {
        static const unsigned NS[] = { 0x10000, 0x100000, 0x4000000 };
        unsigned i;
        for (i = 0; i < sizeof NS / sizeof NS[0]; ++i) {
            BSTR b = SysAllocStringLen(0, NS[i]);   /* a NULL source means "uninitialised" */
            printf("     SysAllocStringLen(NULL, %u) -> %s, len %u\n", NS[i],
                   b ? "allocated" : "NULL", b ? (unsigned)SysStringLen(b) : 0u);
            if (b) SysFreeString(b);
        }
    }
    return 0;
}
