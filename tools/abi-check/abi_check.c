/* tools/abi-check/abi_check.c
 *
 * Gate 3: every implementation in this repository must obey the Win64 register contract.
 *
 * Gates 1 and 2 -- bit-exact correctness and no size class slower than 0.97x -- cannot see this
 * class of bug at all. A function that uses xmm6 as scratch returns exactly the right bytes and
 * runs exactly as fast; it just silently destroys whatever double the CALLER had live. Sixteen
 * implementations here did that for months. It was found by accident, when change 202's benchmark
 * (whose timing accumulators live in xmm6/xmm7) reported a correct function as taking 0.00 ns.
 *
 * Each thunk below performs one real call with real arguments. wia_abi_probe fills every
 * non-volatile register with a sentinel first and reports which ones did not survive. The thunk is
 * ordinary compiled C, so one probe covers every signature -- two arguments or seven -- without
 * knowing anything about them.
 *
 * Build one variant per change:
 *   cl /DT_042 abi_check.c abi_probe.obj ..\..\changes\042-wcsicmp\impl.obj
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern unsigned long long wia_abi_probe(void (*thunk)(void));

static volatile long long sink;

/* Buffers are file-scope and refilled between calls so that each thunk stays trivial. A thunk that
   needed a local of its own in xmm6 would save and restore it, and would mask the very clobber
   this test exists to find. */
static char    a8[512], b8[512];
static wchar_t aw[512], bw[512];

static void fill(void){
    int i;
    for(i = 0; i < 200; i++){ a8[i] = (char)('A' + (i % 26)); b8[i] = (char)('a' + (i % 26)); }
    a8[200] = 0; b8[200] = 0;
    for(i = 0; i < 200; i++){ aw[i] = (wchar_t)('A' + (i % 26)); bw[i] = (wchar_t)('a' + (i % 26)); }
    aw[200] = 0; bw[200] = 0;
}

#if   defined(T_042)
#define NAME "042-wcsicmp"
extern int wia_wcsicmp(const wchar_t*, const wchar_t*);
static void thunk(void){ sink += wia_wcsicmp(aw, bw); }

#elif defined(T_043)
#define NAME "043-stricmp"
extern int wia_stricmp(const char*, const char*);
static void thunk(void){ sink += wia_stricmp(a8, b8); }

#elif defined(T_044)
#define NAME "044-wcsnicmp"
extern int wia_wcsnicmp(const wchar_t*, const wchar_t*, size_t);
static void thunk(void){ sink += wia_wcsnicmp(aw, bw, 200); }

#elif defined(T_045)
#define NAME "045-strnicmp"
extern int wia_strnicmp(const char*, const char*, size_t);
static void thunk(void){ sink += wia_strnicmp(a8, b8, 200); }

#elif defined(T_046)
#define NAME "046-memicmp"
extern int wia_memicmp(const void*, const void*, size_t);
static void thunk(void){ sink += wia_memicmp(a8, b8, 200); }

#elif defined(T_047)
#define NAME "047-strlwr"
extern char* wia_strlwr(char*);
static void thunk(void){ sink += (long long)(size_t)wia_strlwr(a8); }

#elif defined(T_048)
#define NAME "048-strupr"
extern char* wia_strupr(char*);
static void thunk(void){ sink += (long long)(size_t)wia_strupr(a8); }

#elif defined(T_049)
#define NAME "049-wcslwr"
extern wchar_t* wia_wcslwr(wchar_t*);
static void thunk(void){ sink += (long long)(size_t)wia_wcslwr(aw); }

#elif defined(T_050)
#define NAME "050-wcsupr"
extern wchar_t* wia_wcsupr(wchar_t*);
static void thunk(void){ sink += (long long)(size_t)wia_wcsupr(aw); }

#elif defined(T_051)
#define NAME "051-rtlfindcharinunicodestring"
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } US;
extern LONG wia_findchar(ULONG, const US*, const US*, USHORT*);
extern void wia_upcase_init(void);
#define SETUP() wia_upcase_init()
static void thunk(void){
    US s, t; USHORT pos = 0;
    s.Length = 400; s.MaximumLength = 400; s.Buffer = aw;
    t.Length = 4;   t.MaximumLength = 4;   t.Buffer = bw;
    /* flags 0 and 4 take different paths (4 folds through the upcase table); drive both. */
    sink += wia_findchar(0, &s, &t, &pos) + pos;
    sink += wia_findchar(4, &s, &t, &pos) + pos;
}

#elif defined(T_105) || defined(T_107)
#if defined(T_105)
#define NAME "105-cryptstringtobinaryw-base64header"
extern BOOL wia_s2bw_pem(LPCWSTR, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
#define CALL wia_s2bw_pem
#define SRC  L"-----BEGIN X-----\r\nQUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVo=\r\n-----END X-----\r\n"
#define FLG  0u
#else
#define NAME "107-cryptstringtobinaryw-base64any"
extern BOOL wia_s2bw_any(LPCWSTR, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
#define CALL wia_s2bw_any
#define SRC  L"QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVo=\r\n"
#define FLG  7u
#endif
static void thunk(void){
    static BYTE out[256];
    static const wchar_t* s = SRC;
    DWORD cb = sizeof(out), skip = 0, f = 0;
    sink += CALL(s, 0, FLG, out, &cb, &skip, &f) + cb + skip + f;
}

#elif defined(T_125)
#define NAME "125-rtlfindclearbits"
typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RBM;
extern ULONG wia_findclearbits(const RBM*, ULONG, ULONG);
static void thunk(void){
    static ULONG words[512];
    RBM bm; bm.SizeOfBitMap = 512 * 32; bm.Buffer = words;
    words[3] = 0x12345678u;
    sink += wia_findclearbits(&bm, 40, 7);
}

#elif defined(T_161)
#define NAME "161-pathfindfilenamew"
extern PWSTR wia_pathfindfilenamew(PCWSTR);
static void thunk(void){
    static const wchar_t* p = L"C:\\some\\reasonably\\long\\path\\to\\a\\file.txt";
    sink += (long long)(size_t)wia_pathfindfilenamew(p);
}

#elif defined(T_162)
#define NAME "162-pathstrippathw"
extern void wia_pathstrippathw(PWSTR);
static void thunk(void){
    static wchar_t p[64];
    memcpy(p, L"C:\\some\\reasonably\\long\\path\\to\\a\\file.txt", 43 * sizeof(wchar_t));
    wia_pathstrippathw(p);
    sink += p[0];
}

#elif defined(T_202)
#define NAME "202-convertguidtostringw"
extern DWORD wia_ConvertGuidToStringW(const GUID*, PWSTR, DWORD);
static void thunk(void){
    static const GUID g = {0xDEADBEEF,0x1234,0x5678,{0x9A,0xBC,0xDE,0xF0,0x11,0x22,0x33,0x44}};
    sink += wia_ConvertGuidToStringW(&g, bw, 64);
}

#else
#error "define exactly one of T_0xx"
#endif

#ifndef SETUP
#define SETUP() ((void)0)
#endif

static const char* GPN[8] = { "rbx","rbp","rdi","rsi","r12","r13","r14","r15" };

int main(void){
    unsigned long long m = 0;
    char hit[256];
    int i, n = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    hit[0] = 0;
    SETUP();

    /* Several calls: a function that only reaches for xmm6 on its AVX2 path has to be driven down
       that path, and the probe reloads fresh sentinels on every call. */
    for(i = 0; i < 4; i++){ fill(); m = wia_abi_probe(thunk); if(m) break; }

    for(i = 0; i < 8;  i++) if(m & (1ull << i))     { n++; strcat(hit, GPN[i]); strcat(hit, " "); }
    for(i = 0; i < 10; i++) if(m & (1ull << (8+i))) { n++; sprintf(hit + strlen(hit), "xmm%d ", 6+i); }
    if(m & (1ull << 18)) { n++; strcat(hit, "RSP-not-restored "); }
    if(m & (1ull << 19)) { n++; strcat(hit, "DF-set "); }

    if(n){
        printf("ABI: FAILED  %s -- clobbers %d non-volatile register(s): %s\n", NAME, n, hit);
        printf("     Win64 preserves rbx rbp rdi rsi r12-r15 and the LOW 128 BITS of xmm6-xmm15.\n"
               "     The upper halves of ymm6-ymm15 are volatile and are NOT reported here.\n");
        return 1;
    }
    printf("ABI: PASS (%s -- all 8 non-volatile GPRs and xmm6-xmm15 preserved, "
           "stack balanced, DF clear)\n", NAME);
    return 0;
}
