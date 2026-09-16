/* Is the PathRemoveFileSpecW chain worth deriving? Measure before reverse-engineering.
 *
 * The disassembly says PathRemoveFileSpecW is an envelope over five named exports:
 *   PathCchSkipRoot, wcschr (in a last-backslash loop), PathCchIsRoot, PathIsUNCW,
 *   and PathCchRemoveFileSpec -- which is change 240, already landed.
 * Deriving it therefore means deriving PathCchSkipRoot first, which is a full root parser.
 * That is only worth doing if the numbers say so.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef BOOL  (WINAPI *FRFS)(wchar_t*);
typedef HRESULT (WINAPI *FSKIP)(const wchar_t*, const wchar_t**);
typedef HRESULT (WINAPI *FISROOT)(const wchar_t*);
typedef HRESULT (WINAPI *FCCHRFS)(wchar_t*, size_t);

static double best(void (*op)(void*), void* ctx, int inner, int trials)
{
    LARGE_INTEGER f, a, b;
    double bv = 1e300;
    int t, i;
    QueryPerformanceFrequency(&f);
    for (i = 0; i < 64; ++i) op(ctx);
    for (t = 0; t < trials; ++t) {
        QueryPerformanceCounter(&a);
        for (i = 0; i < inner; ++i) op(ctx);
        QueryPerformanceCounter(&b);
        {
            double v = (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)f.QuadPart / inner;
            if (v < bv) bv = v;
        }
    }
    return bv;
}

static FRFS    rfs;
static FSKIP   skip;
static FISROOT isroot;
static FCCHRFS ccrfs;

static wchar_t subj[1200];
static wchar_t work[1200];
static int subjlen;

static volatile int sink;

static void op_rfs(void* c){ (void)c; memcpy(work, subj, (subjlen + 1) * 2); sink += rfs(work); }
static void op_restore(void* c){ (void)c; memcpy(work, subj, (subjlen + 1) * 2); }
static void op_skip(void* c){ const wchar_t* r = 0; (void)c; sink += (int)skip(subj, &r) + (r != 0); }
static void op_isroot(void* c){ (void)c; sink += (int)isroot(subj); }
static void op_ccrfs(void* c){ (void)c; memcpy(work, subj, (subjlen + 1) * 2);
                               sink += (int)ccrfs(work, 1200); }

int main(void)
{
    HMODULE hs = LoadLibraryW(L"shlwapi.dll");
    HMODULE hk = LoadLibraryW(L"kernelbase.dll");
    int i;
    static const struct { const wchar_t* p; const char* what; } T[] = {
        { L"C:\\a\\b\\file.txt",            "short drive path"    },
        { L"\\\\srv\\share\\dir\\file.txt", "short UNC path"      },
        { L"C:\\",                          "a drive root"        },
        { L"file.txt",                      "no separator at all" },
        { 0, 0 },
    };

    rfs    = (FRFS)   GetProcAddress(hs, "PathRemoveFileSpecW");
    skip   = (FSKIP)  GetProcAddress(hk, "PathCchSkipRoot");
    isroot = (FISROOT)GetProcAddress(hk, "PathCchIsRoot");
    ccrfs  = (FCCHRFS)GetProcAddress(hk, "PathCchRemoveFileSpec");
    if (!rfs || !skip || !isroot || !ccrfs) { printf("resolve failed\n"); return 1; }

    SetThreadAffinityMask(GetCurrentThread(), 1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    printf("Is the PathRemoveFileSpecW chain worth deriving? (ns, best of 60)\n");
    printf("  the restore memcpy is charged to the two rows that need it and printed separately\n\n");
    printf("  %-24s %10s %10s %10s %10s %10s\n", "subject", "RemoveFS", "SkipRoot", "IsRoot",
           "CchRemFS", "restore");

    for (i = 0; T[i].p; ++i) {
        double a, b, c, d, r;
        wcscpy(subj, T[i].p);
        subjlen = (int)wcslen(subj);
        a = best(op_rfs, 0, 20000, 60);
        b = best(op_skip, 0, 20000, 60);
        c = best(op_isroot, 0, 20000, 60);
        d = best(op_ccrfs, 0, 20000, 60);
        r = best(op_restore, 0, 20000, 60);
        printf("  %-24s %10.2f %10.2f %10.2f %10.2f %10.2f\n", T[i].what, a, b, c, d, r);
    }

    /* and a long path, which is where a per-character shipped loop would show */
    {
        double a, b, r;
        int k;
        for (k = 0; k < 250; ++k) subj[k] = (k % 9 == 8) ? L'\\' : (wchar_t)(L'a' + k % 26);
        subj[0] = L'C'; subj[1] = L':'; subj[2] = L'\\';
        subj[250] = 0;
        subjlen = 250;
        a = best(op_rfs, 0, 20000, 60);
        b = best(op_skip, 0, 20000, 60);
        r = best(op_restore, 0, 20000, 60);
        printf("  %-24s %10.2f %10.2f %10s %10s %10.2f\n", "250-char drive path", a, b, "-", "-", r);
        printf("\n  PathRemoveFileSpecW on 250 chars, net of the restore: %.2f ns = %.3f ns/char\n",
               a - r, (a - r) / 250.0);
    }
    return 0;
}
