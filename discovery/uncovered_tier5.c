/* discovery/uncovered_tier5.c
 *
 * TIER 5, shaped ntdll exports that tools/uncovered-exports.py still lists and nothing has timed.
 *
 * Tiers 1-4 worked down the FAN-IN ranking: the functions the most loaded modules import. That
 * surface was worked until the remaining leaders were ruled out on evidence (GetSystemTimeAsFileTime
 * at 1.80 ns over 561 modules; QueryPerformanceCounter over 557), and the conclusion recorded there
 * was that it is exhausted of easy wins.
 *
 * This is a different axis: not "who calls it most" but "what SHAPE is it". Every function below is
 * a counted string, a byte-size calculation or a bitmap operation, the shapes this repository
 * already has kernels for, and every one is absent from README.md and from image/tree.
 *
 * The rule this file exists to obey is discovery/README.md's: a candidate becomes a target only
 * after it is timed, and most of that directory is a record of functions that looked slow and were
 * expensive for reasons no assembly can fix. Nothing here is a target yet.
 *
 * Two rows are deliberately CONTROLS rather than candidates:
 *   RtlCopyLuid   copies 8 bytes. If it is not ~1-2 ns, the harness is measuring itself.
 *   RtlClearBits  is the exact sibling of RtlSetBits, which is change 130 and is PARKED on both
 *                 benches. If it measures like its sibling then this tier has learned nothing new,
 *                 and that is worth writing down too.
 *
 * BUILD
 *   . .\tools\vsenv.ps1
 *   cl /nologo /O2 discovery\uncovered_tier5.c /Fe:t5.exe
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef LONG NTSTATUS_;
typedef struct { USHORT Length, MaximumLength; CHAR*  Buffer; } ASTR;
typedef struct { USHORT Length, MaximumLength; WCHAR* Buffer; } USTR;
typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } BITMAP_;

typedef NTSTATUS_ (NTAPI *pfn_AppendStr)(ASTR*, const ASTR*);
typedef void      (NTAPI *pfn_CopyStr)(ASTR*, const ASTR*);
typedef ULONG     (NTAPI *pfn_AnsiSize)(const ASTR*);
typedef ULONG     (NTAPI *pfn_UniToMbSize)(PULONG, const WCHAR*, ULONG);
typedef ULONG     (NTAPI *pfn_MbToUniSize)(PULONG, const CHAR*, ULONG);
typedef ULONG     (NTAPI *pfn_UniStrToAnsiSize)(const USTR*);
typedef BOOLEAN   (NTAPI *pfn_Dos83)(const USTR*, ASTR*, BOOLEAN*);
typedef void      (NTAPI *pfn_CopyLuid)(LUID*, const LUID*);
typedef void      (NTAPI *pfn_ClearBits)(BITMAP_*, ULONG, ULONG);
typedef void      (NTAPI *pfn_InitBitMap)(BITMAP_*, PULONG, ULONG);
typedef NTSTATUS_ (NTAPI *pfn_ValidateUni)(ULONG, const USTR*);
typedef ULONG     (NTAPI *pfn_IsDosDev)(const USTR*);

static double qf;
static void tinit(void){ LARGE_INTEGER f; QueryPerformanceFrequency(&f); qf=(double)f.QuadPart; }
static double nowns(void){ LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart*1e9/qf; }

#define TRIALS 25
#define REPS   2000
static volatile uint64_t sink;
typedef struct { const char* name; double ns; double perb; int bytes; } row_t;
static row_t rows[80]; static int nrows;
static void row(const char* n,double ns,int b){ rows[nrows].name=n; rows[nrows].ns=ns;
    rows[nrows].bytes=b; rows[nrows].perb = b>0 ? ns/b : 0.0; ++nrows; }
#define TIME(label,bytes,body) do{ double best=1e30; int t_, r_; \
    for(t_=0;t_<TRIALS;++t_){ double t0=nowns(); for(r_=0;r_<REPS;++r_){ body; } \
        { double dt=(nowns()-t0)/REPS; if(dt<best) best=dt; } } row(label,best,(bytes)); }while(0)

static void* nt(const char* f){ HMODULE h=GetModuleHandleW(L"ntdll.dll");
    void* p = h ? (void*)GetProcAddress(h,f) : NULL;
    if(!p) printf("  WARN: %s not resolved -- its rows are SKIPPED\n", f);
    return p; }

static char  a8[70000], b8[70000];
static WCHAR w1[35000];
static char  nm1[4][56], nm2[4][56], nm3[4][56], nm4[4][56], nm5[4][56], nm6[4][56];

int main(void){
    int k, i;
    static const int LENS[4] = { 16, 256, 4096, 32000 };
    pfn_AppendStr        pApp;
    pfn_CopyStr          pCpS;
    pfn_AnsiSize         pAnS;
    pfn_UniToMbSize      pU2M;
    pfn_MbToUniSize      pM2U;
    pfn_UniStrToAnsiSize pUAS;
    pfn_Dos83            p83;
    pfn_CopyLuid         pLui;
    pfn_ClearBits        pClr;
    pfn_InitBitMap       pIni;
    pfn_ValidateUni      pVal;
    pfn_IsDosDev         pDev;

    tinit();
    SetThreadAffinityMask(GetCurrentThread(), 1ull<<2);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    pApp = (pfn_AppendStr)nt("RtlAppendStringToString");
    pCpS = (pfn_CopyStr)nt("RtlCopyString");
    pAnS = (pfn_AnsiSize)nt("RtlAnsiStringToUnicodeSize");
    pU2M = (pfn_UniToMbSize)nt("RtlUnicodeToMultiByteSize");
    pM2U = (pfn_MbToUniSize)nt("RtlMultiByteToUnicodeSize");
    pUAS = (pfn_UniStrToAnsiSize)nt("RtlUnicodeStringToAnsiSize");
    p83  = (pfn_Dos83)nt("RtlIsNameLegalDOS8Dot3");
    pLui = (pfn_CopyLuid)nt("RtlCopyLuid");
    pClr = (pfn_ClearBits)nt("RtlClearBits");
    pIni = (pfn_InitBitMap)nt("RtlInitializeBitMap");
    pVal = (pfn_ValidateUni)nt("RtlValidateUnicodeString");
    pDev = (pfn_IsDosDev)nt("RtlIsDosDeviceName_U");

    for (i=0;i<(int)sizeof a8;++i) a8[i]=(char)('a'+(i&15));
    for (i=0;i<35000;++i) w1[i]=(WCHAR)('a'+(i&15));

    /* ---- counted ANSI string work ---- */
    for (k=0;k<4;++k){
        int n=LENS[k];
        ASTR src, dst;
        src.Length=(USHORT)n; src.MaximumLength=(USHORT)n; src.Buffer=a8;
        dst.Length=0;         dst.MaximumLength=(USHORT)60000; dst.Buffer=b8;
        if (pApp){ sprintf(nm1[k],"RtlAppendStringToString     %d B",n);
                   TIME(nm1[k], n, { dst.Length=0; sink+=(ULONG)pApp(&dst,&src); }); }
        if (pCpS){ sprintf(nm2[k],"RtlCopyString               %d B",n);
                   TIME(nm2[k], n, { pCpS(&dst,&src); sink+=dst.Length; }); }
    }

    /* ---- the SIZE calculations: these SCAN, which is why they are here ---- */
    for (k=0;k<4;++k){
        int n=LENS[k]; ULONG out=0;
        ASTR as; USTR us;
        as.Length=(USHORT)n; as.MaximumLength=(USHORT)n; as.Buffer=a8;
        us.Length=(USHORT)(n*2); us.MaximumLength=(USHORT)(n*2); us.Buffer=w1;
        if (pAnS){ sprintf(nm3[k],"RtlAnsiStringToUnicodeSize  %d B",n);
                   TIME(nm3[k], n, sink+=pAnS(&as)); }
        if (pU2M){ sprintf(nm4[k],"RtlUnicodeToMultiByteSize   %d ch",n);
                   TIME(nm4[k], n*2, sink+=pU2M(&out,w1,(ULONG)(n*2))); }
        if (pM2U){ sprintf(nm5[k],"RtlMultiByteToUnicodeSize   %d B",n);
                   TIME(nm5[k], n, sink+=pM2U(&out,a8,(ULONG)n)); }
        if (pUAS){ sprintf(nm6[k],"RtlUnicodeStringToAnsiSize  %d ch",n);
                   TIME(nm6[k], n*2, sink+=pUAS(&us)); }
    }

    /* ---- 8.3 legality and device names: short scans with a rule ---- */
    if (p83){
        static WCHAR okn[] = L"README.TXT";
        static WCHAR non[] = L"a very long name indeed.txt";
        USTR u1, u2; BOOLEAN sp=0;
        u1.Length=(USHORT)(wcslen(okn)*2); u1.MaximumLength=u1.Length; u1.Buffer=okn;
        u2.Length=(USHORT)(wcslen(non)*2); u2.MaximumLength=u2.Length; u2.Buffer=non;
        TIME("RtlIsNameLegalDOS8Dot3  legal",   0, sink+=p83(&u1,NULL,&sp));
        TIME("RtlIsNameLegalDOS8Dot3  illegal", 0, sink+=p83(&u2,NULL,&sp));
    }
    if (pDev){
        static WCHAR dv[] = L"C:\\CON";
        USTR u; u.Length=(USHORT)(wcslen(dv)*2); u.MaximumLength=u.Length; u.Buffer=dv;
        TIME("RtlIsDosDeviceName_U", 0, sink+=pDev(&u));
    }
    if (pVal){
        USTR u; u.Length=8000; u.MaximumLength=8000; u.Buffer=w1;
        TIME("RtlValidateUnicodeString 4000 ch", 8000, sink+=(ULONG)pVal(0,&u));
    }

    /* ---- controls ---- */
    if (pLui){ LUID s, d; s.LowPart=1; s.HighPart=2;
               TIME("CONTROL RtlCopyLuid (8 bytes)", 8, { pLui(&d,&s); sink+=d.LowPart; }); }
    if (pClr && pIni){
        static ULONG buf[2048]; BITMAP_ bm;
        pIni(&bm, buf, 65536);
        TIME("CONTROL RtlClearBits 64Kb, n=40",    0, { pClr(&bm, 100, 40); });
        TIME("CONTROL RtlClearBits 64Kb, n=30000", 0, { pClr(&bm, 0, 30000); });
    }

    printf("\n== TIER 5: shaped ntdll exports nothing has timed ==\n");
    printf("%-42s %12s %12s\n","function (subject)","ns/call","ns/byte");
    printf("----------------------------------------------------------------------------\n");
    for (i=0;i<nrows;++i)
        if (rows[i].perb > 0.0) printf("%-42s %12.2f %12.4f\n", rows[i].name, rows[i].ns, rows[i].perb);
        else                    printf("%-42s %12.2f %12s\n",   rows[i].name, rows[i].ns, "flat");
    printf("----------------------------------------------------------------------------\n");
    printf("A candidate becomes a TARGET only after it is timed. sink=%llu\n",(unsigned long long)sink);
    return 0;
}
