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
#include <wchar.h>

extern unsigned long long wia_abi_probe(void (*thunk)(void));

/* THE WHOLE-THUNK FORM CAN BE MASKED, AND WAS. wia_abi_probe fills the non-volatile registers,
   calls the thunk, and compares afterwards -- but the thunk is compiled C, and if the compiler
   used r15 for a loop variable it saved r15 on entry and restored it on exit, undoing an
   implementation's damage before the comparison. Demonstrated on change 258: with `push r15` and
   its matching `pop` deleted from impl.asm the gate still said PASS, and that build's thunk begins
   with eight pushes including r15. wia_abi_call4 arms the sentinels AROUND THE CALL instead, where
   nothing can restore them; CALL4 collects the result into callmask, which main ORs into the
   verdict. Use it for new work -- a thunk that only uses wia_abi_probe is checked for the
   registers its own compiled code happened to leave alone. */
extern unsigned long long wia_abi_call4(void* fn, unsigned long long a, unsigned long long b,
                                        unsigned long long c, unsigned long long d);
extern unsigned long long wia_abi_call5(void* fn, unsigned long long a, unsigned long long b,
                                        unsigned long long c, unsigned long long d,
                                        unsigned long long e);
static unsigned long long callmask = 0;
#define CALL5(fn, a, b, c, d, e) (callmask |= wia_abi_call5((void*)(fn), (unsigned long long)(a), \
    (unsigned long long)(b), (unsigned long long)(c), (unsigned long long)(d), \
    (unsigned long long)(e)), 0)
#define CALL4(fn, a, b, c, d) (callmask |= wia_abi_call4((void*)(fn), (unsigned long long)(a), \
                               (unsigned long long)(b), (unsigned long long)(c),               \
                               (unsigned long long)(d)))

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

#elif defined(T_203)
#define NAME "203-convertguidtostringa"
extern DWORD wia_ConvertGuidToStringA(const GUID*, PSTR, DWORD);
static void thunk(void){
    static const GUID g = {0xDEADBEEF,0x1234,0x5678,{0x9A,0xBC,0xDE,0xF0,0x11,0x22,0x33,0x44}};
    sink += wia_ConvertGuidToStringA(&g, a8, 64);
}

#elif defined(T_204)
#define NAME "204-rtludiv128"
extern unsigned __int64 wia_udiv128(unsigned __int64, unsigned __int64,
                                    unsigned __int64, unsigned __int64*);
static void thunk(void){
    unsigned __int64 rem = 0;
    /* both paths: the hardware-divide region AND the loop, which spills rbx and rdi itself */
    sink += (long long)wia_udiv128(0x1234ull, 0xDEADBEEFCAFEBABEull, 0x9E3779B97F4A7C15ull, &rem);
    sink += (long long)wia_udiv128(0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 2, &rem);
    sink += (long long)wia_udiv128(0x1234ull, 1000, 0, &rem);   /* divisor 0 -> the loop */
    sink += (long long)rem;
}

#elif defined(T_205)
#define NAME "205-uuidfromstringa"
extern long wia_uuidfromstringa(unsigned char*, GUID*);
static void thunk(void){
    static GUID g;
    /* a valid parse, a rejected one, and the NULL-pointer success path */
    sink += wia_uuidfromstringa((unsigned char*)"deadbeef-1234-5678-9abc-def011223344", &g);
    sink += wia_uuidfromstringa((unsigned char*)"{deadbeef-1234-5678-9abc-def011223344}", &g);
    sink += wia_uuidfromstringa(NULL, &g);
    sink += ((unsigned char*)&g)[0];
}

#elif defined(T_206)
#define NAME "206-stringfromguid2"
extern int wia_StringFromGUID2(const GUID*, wchar_t*, int);
static void thunk(void){
    static const GUID g = {0xDEADBEEF,0x1234,0x5678,{0x9A,0xBC,0xDE,0xF0,0x11,0x22,0x33,0x44}};
    sink += wia_StringFromGUID2(&g, bw, 64);    /* the rendering path */
    sink += wia_StringFromGUID2(&g, bw, 38);    /* and the refusal    */
}

#elif defined(T_207)
#define NAME "207-iidfromstring"
extern long wia_iidfromstring(const wchar_t*, GUID*);
static void thunk(void){
    static GUID g;
    /* a valid parse, a content rejection, and a structural one */
    sink += wia_iidfromstring(L"{DEADBEEF-1234-5678-9ABC-DEF011223344}", &g);
    sink += wia_iidfromstring(L"{ZEADBEEF-1234-5678-9ABC-DEF011223344}", &g);
    sink += wia_iidfromstring(L"{DEADBEEF-1234-5678-9ABC-DEF01122334}",  &g);
    sink += ((unsigned char*)&g)[0];
}

#elif defined(T_208)
#define NAME "208-uuidfromstringw"
extern long wia_uuidfromstringw(wchar_t*, GUID*);
static void thunk(void){
    static GUID g;
    sink += wia_uuidfromstringw(L"deadbeef-1234-5678-9abc-def011223344", &g);
    sink += wia_uuidfromstringw(L"{deadbeef-1234-5678-9abc-def011223344}", &g);
    sink += wia_uuidfromstringw(NULL, &g);
    sink += ((unsigned char*)&g)[0];
}

#elif defined(T_209)
#define NAME "209-lstrcpynw"
extern wchar_t* wia_lstrcpynw(wchar_t*, const wchar_t*, int);
static void thunk(void){
    /* long enough to run the 16-character chunked path, plus a truncating and a no-op call */
    sink += (long long)(size_t)wia_lstrcpynw(bw, aw, 200);
    sink += (long long)(size_t)wia_lstrcpynw(bw, aw, 8);
    sink += (long long)(size_t)wia_lstrcpynw(bw, aw, 0);
}

#elif defined(T_210)
#define NAME "210-comparestringordinal"
extern int  wia_comparestringordinal(const wchar_t*, int, const wchar_t*, int, BOOL);
extern void wia_upcase_init(void);
#define SETUP() wia_upcase_init()
static void thunk(void){
    /* all three ignore-case tiers plus the case-sensitive path and a -1 length */
    sink += wia_comparestringordinal(aw, 200, bw, 200, FALSE);
    sink += wia_comparestringordinal(aw, 200, aw, 200, TRUE);      /* tier 1: equal raw   */
    sink += wia_comparestringordinal(aw, 200, bw, 200, TRUE);      /* tier 2: ASCII fold  */
    sink += wia_comparestringordinal(L"3031", 2, L"1011", 2, TRUE);  /* tier 3 */
    sink += wia_comparestringordinal(aw, -1, bw, -1, FALSE);
}

#elif defined(T_211)
#define NAME "211-lstrcpyna"
extern char* wia_lstrcpyna(char*, const char*, int);
static void thunk(void){
    /* long enough to run the PAIRED 64-byte path, a length that lands in the single-chunk loop,
       one that reaches the clamped short path, plus a truncating and a no-op call */
    sink += (long long)(size_t)wia_lstrcpyna(b8, a8, 200);
    sink += (long long)(size_t)wia_lstrcpyna(b8, a8, 40);
    sink += (long long)(size_t)wia_lstrcpyna(b8, a8, 8);
    sink += (long long)(size_t)wia_lstrcpyna(b8, a8, 0);
}

#elif defined(T_212)
#define NAME "212-pathfindfilenamea"
extern const char* wia_pathfindfilenamea(const char*);
static void thunk(void){
    /* a long path (whole-block skipping), one with no separator at all, and the NULL exit */
    static const char* p = "C:\\some\\reasonably\\long\\path\\to\\a\\file.txt";
    sink += (long long)(size_t)wia_pathfindfilenamea(p);
    sink += (long long)(size_t)wia_pathfindfilenamea(a8);
    sink += (long long)(size_t)wia_pathfindfilenamea(0);
}

#elif defined(T_213)
#define NAME "213-strrchra"
extern const char* wia_strrchra(const char*, const char*, WORD);
static void thunk(void){
    /* the unbounded forward path, the bounded backward path, a hit, a miss, and both NULL exits */
    sink += (long long)(size_t)wia_strrchra(a8, 0, (WORD)0x5A);
    sink += (long long)(size_t)wia_strrchra(a8, 0, (WORD)0x42);
    sink += (long long)(size_t)wia_strrchra(a8, a8 + 200, (WORD)0x42);
    sink += (long long)(size_t)wia_strrchra(a8, a8 + 7,   (WORD)0x42);
    sink += (long long)(size_t)wia_strrchra(a8, 0, (WORD)0x5A00);
    sink += (long long)(size_t)wia_strrchra(0,  0, (WORD)0x42);
}

#elif defined(T_214)
#define NAME "214-strcspna"
extern int wia_strcspna(const char*, const char*);
static void thunk(void){
    /* a full scan, an early hit, a set spanning BOTH halves of the bitmap, and both NULL exits.
       The set-building loop writes into the CALLER'S SHADOW SPACE, so this thunk also proves that
       doing so leaves the caller's frame intact. */
    sink += wia_strcspna(a8, "#");
    sink += wia_strcspna(a8, "CD");
    sink += wia_strcspna(a8, "AÃ");
    sink += wia_strcspna(a8, "");
    sink += wia_strcspna(a8, 0);
    sink += wia_strcspna(0, "#");
}

#elif defined(T_215)
#define NAME "215-strpbrka"
extern const char* wia_strpbrka(const char*, const char*);
static void thunk(void){
    /* a hit, a full scan to the terminator, a set spanning BOTH halves of the bitmap, the empty
       set and both NULL exits. The set-building loop writes into the CALLER'S SHADOW SPACE, so
       this also proves that leaves the caller's frame intact. */
    sink += (long long)(size_t)wia_strpbrka(a8, "CD");
    sink += (long long)(size_t)wia_strpbrka(a8, "#");
    sink += (long long)(size_t)wia_strpbrka(a8, "\x41\xC3");
    sink += (long long)(size_t)wia_strpbrka(a8, "");
    sink += (long long)(size_t)wia_strpbrka(a8, 0);
    sink += (long long)(size_t)wia_strpbrka(0, "#");
}

#elif defined(T_216)
#define NAME "216-strspna"
extern int wia_strspna(const char*, const char*);
static void thunk(void){
    /* a span that stops early, one that runs to the terminator (the inverted mask's own stop),
       a set spanning BOTH halves of the bitmap, the empty set and both NULL exits */
    sink += wia_strspna(a8, "ABCD");
    sink += wia_strspna(a8, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    sink += wia_strspna(a8, "\x41\xC3");
    sink += wia_strspna(a8, "");
    sink += wia_strspna(a8, 0);
    sink += wia_strspna(0, "A");
}

#elif defined(T_132)
#define NAME "132-pathfindextensionw"
extern const wchar_t* wia_pathfindextw(const wchar_t*);
static void thunk(void){
    /* added when this change was CORRECTED for the missing space rule: the amendment introduced a
       second vector temp, and a callee-saved one would have been invisible to correctness */
    sink += (long long)(size_t)wia_pathfindextw(L"C:\\some\\long\\path\\to\\a\\file.txt");
    sink += (long long)(size_t)wia_pathfindextw(L"a name with spaces.txt ");
    sink += (long long)(size_t)wia_pathfindextw(aw);
}

#elif defined(T_217)
#define NAME "217-pathfindextensiona"
extern const char* wia_pathfindexta(const char*);
static void thunk(void){
    sink += (long long)(size_t)wia_pathfindexta("C:\\some\\long\\path\\to\\a\\file.txt");
    sink += (long long)(size_t)wia_pathfindexta("a name with spaces.txt ");
    sink += (long long)(size_t)wia_pathfindexta(a8);
    sink += (long long)(size_t)wia_pathfindexta(0);
}

#elif defined(T_140)
#define NAME "140-pathremoveextensionw"
extern void wia_pathremoveextw(wchar_t*);
static void thunk(void){
    /* added when the space rule was corrected: the amendment introduced a second vector temp */
    static wchar_t p[64];
    memcpy(p, L"C:\\some\\long\\path\\to\\a\\file.txt", 33 * sizeof(wchar_t));
    wia_pathremoveextw(p);
    sink += p[0];
    memcpy(p, L"a name with spaces.txt ", 24 * sizeof(wchar_t));
    wia_pathremoveextw(p);
    sink += p[0];
}

#elif defined(T_143)
#define NAME "143-pathcchfindextension"
extern long wia_pathcchfindext(const wchar_t*, unsigned long long, const wchar_t**);
static void thunk(void){
    const wchar_t* ext = 0;
    sink += wia_pathcchfindext(L"C:\\dir\\file.txt", 17, &ext);
    sink += (long long)(size_t)ext;
    sink += wia_pathcchfindext(L"a name with spaces.txt ", 24, &ext);
    sink += (long long)(size_t)ext;
}

#elif defined(T_144)
#define NAME "144-pathcchremoveextension"
extern long wia_pathcchremoveext(wchar_t*, unsigned long long);
static void thunk(void){
    static wchar_t p[64];
    memcpy(p, L"C:\\dir\\file.txt", 17 * sizeof(wchar_t));
    sink += wia_pathcchremoveext(p, 17);
    memcpy(p, L"a name with spaces.txt ", 24 * sizeof(wchar_t));
    sink += wia_pathcchremoveext(p, 24);
    sink += p[0];
}

#elif defined(T_158)
#define NAME "158-pathrenameextensionw"
extern int wia_pathrenameextw(wchar_t*, const wchar_t*);
static void thunk(void){
    /* added when this change was CORRECTED for the missing space rule -- the amendment introduced
       a second vector temp per block, and a callee-saved one would be invisible to correctness */
    static wchar_t p[512];
    memcpy(p, L"C:\\some\\long\\path\\to\\a\\file.txt", 33 * sizeof(wchar_t));
    sink += wia_pathrenameextw(p, L".obj");
    sink += p[0];
    memcpy(p, L"a name with spaces.txt ", 24 * sizeof(wchar_t));
    sink += wia_pathrenameextw(p, L".obj");     /* the space case the correction is about */
    sink += p[0];
    { int i; for (i = 0; i < 300; ++i) p[i] = L'a'; p[300] = 0; }
    sink += wia_pathrenameextw(p, L".obj");     /* past MAX_PATH: the failure exit */
    sink += p[0];
}

#elif defined(T_159)
#define NAME "159-pathcchrenameextension"
extern long wia_pathcchrenameext(wchar_t*, unsigned long long, const wchar_t*);
static void thunk(void){
    static wchar_t p[512];
    memcpy(p, L"C:\\dir\\file.txt", 17 * sizeof(wchar_t));
    sink += wia_pathcchrenameext(p, 64, L".obj");
    sink += p[0];
    memcpy(p, L"a name with spaces.txt ", 24 * sizeof(wchar_t));
    sink += wia_pathcchrenameext(p, 64, L".obj");
    sink += p[0];
    memcpy(p, L"C:\\dir\\file.txt", 17 * sizeof(wchar_t));
    sink += wia_pathcchrenameext(p, 4, L".obj");   /* the insufficient-buffer partial write */
    sink += p[0];
    sink += wia_pathcchrenameext(0, 64, L".obj");  /* E_INVALIDARG */
}

#elif defined(T_160)
#define NAME "160-pathcchaddextension"
extern long wia_pathcchaddext(wchar_t*, unsigned long long, const wchar_t*);
static void thunk(void){
    static wchar_t p[512];
    memcpy(p, L"C:\\dir\\file", 13 * sizeof(wchar_t));
    sink += wia_pathcchaddext(p, 64, L".obj");     /* appends */
    sink += p[0];
    memcpy(p, L"C:\\dir\\file.txt", 17 * sizeof(wchar_t));
    sink += wia_pathcchaddext(p, 64, L".obj");     /* S_FALSE, nothing written */
    sink += p[0];
    memcpy(p, L"a name with spaces.txt ", 24 * sizeof(wchar_t));
    sink += wia_pathcchaddext(p, 64, L".obj");     /* the space case the correction is about */
    sink += p[0];
    sink += wia_pathcchaddext(0, 64, L".obj");     /* E_INVALIDARG */
}

#elif defined(T_174)
#define NAME "174-pathundecoratew"
extern void wia_pathundecoratew(wchar_t*);
static void thunk(void){
    /* added when this change was CORRECTED for the missing space rule. The correction PUSHES RBX
       to carry the second tracked position, so this change went from using no callee-saved
       register at all to using one -- exactly the edit this gate exists to police. */
    static wchar_t p[512];
    memcpy(p, L"C:\\dir\\file[1].txt", 20 * sizeof(wchar_t));
    wia_pathundecoratew(p);                        /* an ordinary removal */
    sink += p[0];
    memcpy(p, L"file[1]x.txt", 13 * sizeof(wchar_t));
    wia_pathundecoratew(p);                        /* nothing hugs the dot: unchanged */
    sink += p[0];
    memcpy(p, L". []", 5 * sizeof(wchar_t));
    wia_pathundecoratew(p);                        /* the space case the correction is about */
    sink += p[0];
    { int i; for (i = 0; i < 400; ++i) p[i] = L'a'; p[400] = 0; }
    wia_pathundecoratew(p);                        /* a long run through the vector scan */
    sink += p[0];
}

#elif defined(T_223)
#define NAME "223-pathundecoratea"
extern void wia_pathundecoratea(char*);
static void thunk(void){
    /* this change PUSHES RBX to carry the second tracked position (the last backslash-or-space,
       which conjunct (b) needs and conjunct (d) must not have), so the gate matters here */
    static char p[512];
    memcpy(p, "C:\\dir\\file[1].txt", 20);
    wia_pathundecoratea(p);                /* an ordinary removal */
    sink += p[0];
    memcpy(p, "file[1]x.txt", 13);
    wia_pathundecoratea(p);                /* nothing hugs the dot: unchanged */
    sink += p[0];
    memcpy(p, ". []", 5);
    wia_pathundecoratea(p);                /* the space case the whole rule turns on */
    sink += p[0];
    memcpy(p, "a b[1].txt", 11);
    wia_pathundecoratea(p);                /* the asymmetry: a space does NOT start a component */
    sink += p[0];
    { int i; for (i = 0; i < 400; ++i) p[i] = 'a'; p[392]='['; p[393]='1'; p[394]=']';
      p[395]='.'; p[400] = 0; }
    wia_pathundecoratea(p);                /* a long run through the vector scan and block move */
    sink += p[0];
    wia_pathundecoratea(0);                /* NULL: must still balance the stack */
}

#elif defined(T_224)
#define NAME "224-pathrenameextensiona"
extern int wia_pathrenameexta(char*, const char*);
static void thunk(void){
    /* this change PUSHES RBX to carry the last backslash-or-space, and it has THREE exits --
       the two NULL rejections, the MAX_PATH rejection and the success path -- every one of
       which has to pop it */
    static char p[640];
    memcpy(p, "C:\\dir\\file.txt", 17);
    sink += wia_pathrenameexta(p, ".obj");     /* an ordinary replacement */
    sink += p[0];
    memcpy(p, "file", 5);
    sink += wia_pathrenameexta(p, ".obj");     /* no extension: appended */
    sink += p[0];
    memcpy(p, "a.b c", 6);
    sink += wia_pathrenameexta(p, ".obj");     /* the SPACE case: the dot is suppressed */
    sink += p[0];
    { int i; for (i = 0; i < 300; ++i) p[i] = 'a'; p[296] = '.'; p[300] = 0; }
    sink += wia_pathrenameexta(p, ".obj");     /* past the MAX_PATH limit: FALSE, untouched */
    sink += p[0];
    { int i; for (i = 0; i < 200; ++i) p[i] = 'a'; p[100] = ' '; p[196] = '.'; p[200] = 0; }
    sink += wia_pathrenameexta(p, ".obj");     /* a long run through the vector scan */
    sink += p[0];
    sink += wia_pathrenameexta(p, 0);          /* NULL extension */
    sink += wia_pathrenameexta(0, ".obj");     /* NULL path */
}

#elif defined(T_225)
#define NAME "225-lstrlena"
extern int wia_lstrlena(const char*);
static void thunk(void){
    /* The core uses no callee-saved register, but the SEH WRAPPER is compiled C and the fault path
       unwinds THROUGH it -- so the interesting case here is not the ordinary call, it is the one
       that faults. An unwind that restored the wrong registers, or left the upper YMM halves
       dirty, would be invisible to correctness: the return value is 0 either way. */
    static char p[4200];
    int i;
    for (i = 0; i < 4000; ++i) p[i] = 'a';
    p[4000] = 0;
    sink += wia_lstrlena(p);               /* long: the paired loop */
    p[7] = 0;
    sink += wia_lstrlena(p);               /* short: the masked first block only */
    sink += wia_lstrlena(p + 1);           /* unaligned start */
    sink += wia_lstrlena("");              /* empty */
    sink += wia_lstrlena(0);               /* NULL */
    {
        SYSTEM_INFO si; SIZE_T pg; char* base; DWORD old; int tail;
        GetSystemInfo(&si);
        pg = si.dwPageSize;
        base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        if (base) {
            VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
            for (tail = 1; tail <= 40; ++tail) {
                char* q = (base+pg) - tail;
                for (i = 0; i < tail; ++i) q[i] = 'a';   /* NO terminator: this one FAULTS */
                sink += wia_lstrlena(q);
            }
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }
}

#elif defined(T_226)
#define NAME "226-pathremoveargsa"
extern void wia_pathremoveargsa(char*);
static void thunk(void){
    /* this change PUSHES RBX to carry the quote parity across 32-byte blocks, and it has four
       exits -- NULL, nothing-to-do, the trailing trim and the two-terminator split */
    static char p[640];
    memcpy(p, "C:\\dir\\app.exe -x", 18);
    wia_pathremoveargsa(p);                  /* an ordinary split */
    sink += p[0];
    memcpy(p, "ab   c", 7);
    wia_pathremoveargsa(p);                  /* behaviour 2: TWO terminators written */
    sink += p[0] + p[4];
    memcpy(p, "abc   ", 7);
    wia_pathremoveargsa(p);                  /* behaviour 3: the trailing trim */
    sink += p[0];
    memcpy(p, "abcdef", 7);
    wia_pathremoveargsa(p);                  /* nothing to do: writes nothing at all */
    sink += p[0];
    memcpy(p, "\"a b\" c", 8);
    wia_pathremoveargsa(p);                  /* the quoted space does not split */
    sink += p[0];
    { int i; for (i = 0; i < 300; ++i) p[i] = 'a';
      p[1] = '"'; p[150] = ' '; p[250] = '"'; p[280] = ' '; p[300] = 0; }
    wia_pathremoveargsa(p);                  /* a quote parity carried across many blocks */
    sink += p[0];
    wia_pathremoveargsa(0);                  /* NULL */
}

#elif defined(T_227)
#define NAME "227-lstrcpya"
extern char* wia_lstrcpya(char*, const char*);
static void thunk(void){
    /* As with 225, the interesting case is not the ordinary call but the ones that FAULT: the
       fault path unwinds through compiled C, and an unwind that restored the wrong registers or
       left the upper YMM halves dirty is invisible to correctness, because the return is NULL
       either way. Both fault shapes are driven here -- a bad SOURCE and a short DESTINATION. */
    static char d[8300];
    static char s[4200];
    int i;
    for (i = 0; i < 4000; ++i) s[i] = 'a';
    s[4000] = 0;
    sink += (long long)(size_t)wia_lstrcpya(d, s);          /* long: the 64-byte loop */
    sink += d[0];
    s[7] = 0;
    sink += (long long)(size_t)wia_lstrcpya(d, s);          /* short: the tail ladder only */
    sink += (long long)(size_t)wia_lstrcpya(d + 1, s + 1);  /* both unaligned */
    sink += (long long)(size_t)wia_lstrcpya(d, "");         /* empty */
    sink += (long long)(size_t)wia_lstrcpya(d, 0);          /* NULL source */
    sink += (long long)(size_t)wia_lstrcpya(0, "abc");      /* NULL destination */
    {
        SYSTEM_INFO si; SIZE_T pg; char* base; DWORD old; int tail;
        GetSystemInfo(&si);
        pg = si.dwPageSize;
        base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        if (base) {
            VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
            /* a faulting SOURCE */
            for (tail = 1; tail <= 40; ++tail) {
                char* q = (base+pg) - tail;
                for (i = 0; i < tail; ++i) q[i] = 'a';      /* NO terminator */
                sink += (long long)(size_t)wia_lstrcpya(d, q);
            }
            /* a faulting DESTINATION */
            for (i = 0; i < 400; ++i) s[i] = 'b';
            s[400] = 0;
            for (tail = 1; tail <= 40; ++tail) {
                char* q = (base+pg) - tail;
                sink += (long long)(size_t)wia_lstrcpya(q, s);
            }
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }
}

#elif defined(T_229)
#define NAME "229-lstrcpyw"
extern wchar_t* wia_lstrcpyw(wchar_t*, const wchar_t*);
static void thunk(void){
    /* the fault paths unwind through compiled C, and an unwind that restored the wrong registers
       or left the upper YMM halves dirty is invisible to correctness because the return is NULL
       either way -- so both fault shapes are driven, INCLUDING the odd-aligned destination that
       the whole-character clamp exists for */
    static wchar_t d[8300];
    static wchar_t s[4200];
    int i;
    for (i = 0; i < 4000; ++i) s[i] = L'a';
    s[4000] = 0;
    sink += (long long)(size_t)wia_lstrcpyw(d, s);          /* long: the 64-byte loop */
    sink += d[0];
    s[7] = 0;
    sink += (long long)(size_t)wia_lstrcpyw(d, s);          /* short: the tail ladder only */
    sink += (long long)(size_t)wia_lstrcpyw(d + 1, s + 1);  /* both unaligned */
    sink += (long long)(size_t)wia_lstrcpyw(d, L"");        /* empty */
    sink += (long long)(size_t)wia_lstrcpyw(d, 0);          /* NULL source */
    sink += (long long)(size_t)wia_lstrcpyw(0, L"abc");     /* NULL destination */
    {
        SYSTEM_INFO si; SIZE_T pg; char* base; DWORD old; int tail;
        GetSystemInfo(&si);
        pg = si.dwPageSize;
        base = (char*)VirtualAlloc(0, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        if (base) {
            VirtualProtect(base+pg, pg, PAGE_NOACCESS, &old);
            for (tail = 1; tail <= 40; ++tail) {            /* a faulting SOURCE */
                wchar_t* q = (wchar_t*)(base+pg) - tail;
                for (i = 0; i < tail; ++i) q[i] = L'a';     /* NO terminator */
                sink += (long long)(size_t)wia_lstrcpyw(d, q);
            }
            for (i = 0; i < 400; ++i) s[i] = L'b';
            s[400] = 0;
            for (tail = 1; tail <= 41; ++tail) {            /* a faulting DESTINATION, ODD and even
                                                               widths in BYTES */
                char* q = (base+pg) - tail;
                sink += (long long)(size_t)wia_lstrcpyw((wchar_t*)q, s);
            }
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }
}

#elif defined(T_231)
#define NAME "231-strcatbuffa"
extern char* wia_strcatbuffa(char*, const char*, int);
static void thunk(void){
    /* No SEH wrapper on this one -- the shipped export FAULTS rather than swallowing, so there is
       no fault path to unwind through. Every exit is driven instead: NULL destination, NULL source,
       cch <= 0, the scan failing (nothing written), an exact fit, a truncating append, and the
       long path through the 32-byte chunks. */
    static char p[2048];
    static char s[1024];
    int i;
    for (i = 0; i < 900; ++i) s[i] = 'Z';
    s[900] = 0;
    memcpy(p, "abc", 4);
    sink += (long long)(size_t)wia_strcatbuffa(p, "de", 40);      /* an ordinary append */
    sink += p[0];
    memcpy(p, "abc", 4);
    sink += (long long)(size_t)wia_strcatbuffa(p, "defghij", 6);  /* truncating */
    sink += p[0];
    memcpy(p, "abc", 4);
    sink += (long long)(size_t)wia_strcatbuffa(p, "de", 4);       /* exactly full: no room */
    sink += p[0];
    for (i = 0; i < 100; ++i) p[i] = 'a';
    p[100] = 0;
    sink += (long long)(size_t)wia_strcatbuffa(p, "x", 50);       /* scan fails: writes NOTHING */
    sink += p[0];
    for (i = 0; i < 1000; ++i) p[i] = 'a';
    p[1000] = 0;
    sink += (long long)(size_t)wia_strcatbuffa(p, s, 1900);       /* long: the 32-byte chunks */
    sink += p[0];
    sink += (long long)(size_t)wia_strcatbuffa(p, "x", 0);        /* cch 0 */
    sink += (long long)(size_t)wia_strcatbuffa(p, "x", -5);       /* negative cch */
    sink += (long long)(size_t)wia_strcatbuffa(p, 0, 40);         /* NULL source */
    sink += (long long)(size_t)wia_strcatbuffa(0, "x", 40);       /* NULL destination */
}

#elif defined(T_232)
#define NAME "232-pathremovebackslasha"
extern char* wia_pathremovebackslasha(char*);
static void thunk(void){
    static char p[1200];
    int i;
    memcpy(p, "C:\\dir\\sub\\", 13);
    sink += (long long)(size_t)wia_pathremovebackslasha(p);   /* an ordinary strip */
    sink += p[0];
    memcpy(p, "C:\\", 4);
    sink += (long long)(size_t)wia_pathremovebackslasha(p);   /* a protected drive root */
    sink += p[0];
    memcpy(p, "\\\\", 3);
    sink += (long long)(size_t)wia_pathremovebackslasha(p);   /* the protected UNC root */
    sink += p[0];
    memcpy(p, "", 1);
    sink += (long long)(size_t)wia_pathremovebackslasha(p);   /* empty: returns psz itself */
    for (i = 0; i < 1000; ++i) p[i] = 'a';
    p[999] = '\\'; p[1000] = 0;
    sink += (long long)(size_t)wia_pathremovebackslasha(p);   /* long: the paired scan */
    sink += p[0];
    sink += (long long)(size_t)wia_pathremovebackslasha(0);   /* NULL */
}

#elif defined(T_233)
#define NAME "233-pathquotespacesa"
extern int wia_pathquotespacesa(char*);
static void thunk(void){
    static char p[700];
    int i;
    memcpy(p, "a b", 4);
    sink += wia_pathquotespacesa(p);            /* the quoting path */
    sink += p[0];
    memcpy(p, "nospace", 8);
    sink += wia_pathquotespacesa(p);            /* refused: nothing written */
    sink += p[0];
    for (i = 0; i < 257; ++i) p[i] = 'a';
    p[100] = ' '; p[257] = 0;
    sink += wia_pathquotespacesa(p);            /* exactly at the 257 cap */
    sink += p[0];
    for (i = 0; i < 300; ++i) p[i] = 'a';
    p[100] = ' '; p[300] = 0;
    sink += wia_pathquotespacesa(p);            /* over the cap: refused */
    sink += p[0];
    sink += wia_pathquotespacesa(0);            /* NULL */
}

#elif defined(T_234)
#define NAME "234-pathfindnextcomponenta"
extern char* wia_pathfindnextcomponenta(const char*);
static void thunk(void){
    static char p[4200];
    int i;
    sink += (long long)(size_t)wia_pathfindnextcomponenta("C:\\dir\\file");
    sink += (long long)(size_t)wia_pathfindnextcomponenta("nosep");
    sink += (long long)(size_t)wia_pathfindnextcomponenta("\\\\server");  /* the doubled rule */
    sink += (long long)(size_t)wia_pathfindnextcomponenta("");             /* NULL */
    sink += (long long)(size_t)wia_pathfindnextcomponenta(0);              /* NULL */
    for (i = 0; i < 4000; ++i) p[i] = 'a';
    p[4000] = 0;
    sink += (long long)(size_t)wia_pathfindnextcomponenta(p);              /* the long scan */
}

#elif defined(T_235)
#define NAME "235-pathisfilespeca"
extern int wia_pathisfilespeca(const char*);
static void thunk(void){
    /* a clean name (the scan runs to the terminator), both separators, the EMPTY STRING -- which is
       TRUE, the one case a natural model gets wrong -- NULL, and a 4000-byte scan so the vector loop
       runs many iterations before answering. */
    static char p[4200];
    int i;
    sink += wia_pathisfilespeca("file.txt");
    sink += wia_pathisfilespeca("a\\b");         /* backslash */
    sink += wia_pathisfilespeca("a:b");           /* colon -- the other separator */
    sink += wia_pathisfilespeca("");              /* TRUE */
    sink += wia_pathisfilespeca(0);               /* NULL -> 0 */
    for (i = 0; i < 4000; ++i) p[i] = 'a';
    p[4000] = 0;
    sink += wia_pathisfilespeca(p);               /* the long scan */
}

#elif defined(T_236)
#define NAME "236-pathcommonprefixa"
extern int wia_pathcommonprefixa(const char*, const char*, char*);
static void thunk(void){
    /* the fold path (differing case forces the 44-instruction vector fold), the cut, the
       length-2 fixup, the MAX_PATH refusal, a NULL buffer, and NULL paths. The 300-character
       pair drives both the vector loop and the copy. */
    static char p[600], q[600], o[600];
    int i;
    sink += wia_pathcommonprefixa("C:\\dir\\a", "C:\\dir\\b", o);
    sink += o[0];
    sink += wia_pathcommonprefixa("C:\\DIR\\a", "c:\\dir\\b", o);  /* folds */
    sink += o[0];
    sink += wia_pathcommonprefixa("aa", "aa", o);          /* reports 3, writes 2 */
    sink += o[0];
    sink += wia_pathcommonprefixa("abc", "xyz", o);        /* writes a bare terminator */
    sink += o[0];
    sink += wia_pathcommonprefixa("C:\\a", 0, o);       /* NULL writes nothing */
    sink += wia_pathcommonprefixa(0, 0, 0);
    for (i = 0; i < 300; ++i) { p[i] = (i % 8 == 7) ? 0x5C : (char)(0x61 + i % 23); q[i] = p[i]; }
    p[300] = 0; q[300] = 0;
    sink += wia_pathcommonprefixa(p, q, o);                /* past MAX_PATH: refuses the copy */
    sink += o[0];
    q[100] = 0x5A;
    sink += wia_pathcommonprefixa(p, q, o);                /* a long scan, then a real copy */
    sink += o[0];
    sink += wia_pathcommonprefixa(p, q, 0);                /* no buffer at all */
}

#elif defined(T_237)
#define NAME "237-pathisprefixa"
extern int wia_pathisprefixa(const char*, const char*);
static void thunk(void){
    /* a TRUE and a FALSE, the fold path (differing case forces the vector fold), both anomalies
       that fall out of the reported-count defect, NULL, and a 300-character scan. */
    static char p[600], q[600];
    int i;
    sink += wia_pathisprefixa("C:\\dir", "C:\\dir\\file");
    sink += wia_pathisprefixa("C:\\DIR", "c:\\dir\\file");  /* folds */
    sink += wia_pathisprefixa("C:\\dir", "C:\\dirfile");    /* not a boundary */
    sink += wia_pathisprefixa("aa", "aa");                 /* NOT a prefix of itself */
    sink += wia_pathisprefixa("xy\\", "xy");            /* longer IS a prefix */
    sink += wia_pathisprefixa("", "C:\\dir");           /* the empty string is TRUE */
    sink += wia_pathisprefixa(0, "C:\\a");
    sink += wia_pathisprefixa("C:\\a", 0);
    for (i = 0; i < 300; ++i) { q[i] = (i % 8 == 7) ? 0x5C : (char)(0x61 + i % 23); p[i] = q[i]; }
    q[300] = 0; p[255] = 0;
    sink += wia_pathisprefixa(p, q);                       /* a long TRUE */
    for (i = 0; i < 255; ++i) if (p[i] >= 0x61 && p[i] <= 0x7A) p[i] = (char)(p[i] - 0x20);
    sink += wia_pathisprefixa(p, q);                       /* a long TRUE through the FOLD */
    p[100] = 0x51;
    sink += wia_pathisprefixa(p, q);                       /* a long FALSE */
}

#elif defined(T_238)
#define NAME "238-pathmakeprettya"
extern int wia_pathmakeprettya(char*);
static void thunk(void){
    /* a rewrite, a refusal, the CP1252 branch the ASCII veto ignores, index 0 being UPPERCASED,
       the empty string, NULL, and a 300-character path that crosses the 259 truncation bound. */
    static char p[600];
    int i;
    memcpy(p, "C:\\\\DIR\\\\FILE.TXT", 16);
    sink += wia_pathmakeprettya(p);            /* rewritten */
    sink += p[0];
    memcpy(p, "C:\\\\Dir\\\\FILE.TXT", 16);
    sink += wia_pathmakeprettya(p);            /* refused: writes nothing */
    sink += p[0];
    p[0] = (char)0xE0; p[1] = 0x42; p[2] = 0x43; p[3] = 0;
    sink += wia_pathmakeprettya(p);            /* index 0 UPPERCASED, 0xE0 -> 0xC0 */
    sink += p[0];
    p[0] = 0;
    sink += wia_pathmakeprettya(p);            /* the empty string */
    sink += wia_pathmakeprettya(0);            /* NULL -> 0 */
    for (i = 0; i < 300; ++i) p[i] = (i % 8 == 7) ? 0x5C : (char)(0x41 + i % 23);
    p[300] = 0;
    sink += wia_pathmakeprettya(p);            /* crosses the 259 truncation bound */
    sink += p[0];
    for (i = 0; i < 300; ++i) p[i] = (char)(0x41 + i % 23);
    p[290] = 0x71; p[300] = 0;
    sink += wia_pathmakeprettya(p);            /* vetoed by a letter PAST the rewrite bound */
    sink += p[0];
}

#elif defined(T_142)
#define NAME "142-pathaddbackslashw"
extern wchar_t* wia_pathaddbackslashw(wchar_t*);
static void thunk(void){
    /* appends, declines because it already ends in one, declines because a forward slash does NOT
       count, the empty string (left alone), and both sides of the MAX_PATH rule -- which is applied
       BEFORE the already-terminated shortcut, so the two thresholds differ by one.

       NO NULL CASE HERE, deliberately, and it is not an oversight: unlike the lstrcat pair below,
       PathAddBackslashW has no NULL contract -- the shipped export dereferences its argument and so
       does this one, with no SEH wrapper to swallow it. Calling it with NULL crashes the driver
       before it can report anything, which is exactly what this thunk did on its first run. */
    static wchar_t p[600];
    int i;
    wcscpy(p, L"C:\\dir");
    sink += (long long)(size_t)wia_pathaddbackslashw(p);
    sink += p[0];
    wcscpy(p, L"C:\\dir\\");
    sink += (long long)(size_t)wia_pathaddbackslashw(p);
    wcscpy(p, L"C:\\dir/");
    sink += (long long)(size_t)wia_pathaddbackslashw(p);   /* a slash does not count */
    sink += p[0];
    wcscpy(p, L"");
    sink += (long long)(size_t)wia_pathaddbackslashw(p);
    for (i = 0; i < 258; ++i) p[i] = (wchar_t)(0x61 + i % 23);
    p[258] = 0;
    sink += (long long)(size_t)wia_pathaddbackslashw(p);   /* 258: the last length that fits */
    for (i = 0; i < 259; ++i) p[i] = (wchar_t)(0x61 + i % 23);
    p[259] = 0;
    sink += (long long)(size_t)wia_pathaddbackslashw(p);   /* 259: refused, returns NULL */
    for (i = 0; i < 259; ++i) p[i] = (wchar_t)(0x61 + i % 23);
    p[258] = 0x5C; p[259] = 0;
    sink += (long long)(size_t)wia_pathaddbackslashw(p);   /* already terminated at 259: still fits */
    for (i = 0; i < 500; ++i) p[i] = (wchar_t)(0x61 + i % 23);
    p[500] = 0;
    sink += (long long)(size_t)wia_pathaddbackslashw(p);   /* long: the vector scan, then refused */
    sink += p[0];
}

#elif defined(T_228)
#define NAME "228-lstrcata"
extern char* wia_lstrcata(char*, const char*);
static void thunk(void){
    /* the destination scan then the page-clamped copy, at lengths that reach every path: an empty
       destination, a short append, one crossing 32 bytes, a long source, and every NULL combination --
       which this export SWALLOWS rather than faulting. */
    static char d[9000];
    static char s[5000];
    int i;
    strcpy(s, "APPENDED");
    strcpy(d, "");      sink += (long long)(size_t)wia_lstrcata(d, s); sink += d[0];
    strcpy(d, "abcdefgh"); sink += (long long)(size_t)wia_lstrcata(d, s); sink += d[0];
    strcpy(d, "0123456789012345678901234567890123456789");
    sink += (long long)(size_t)wia_lstrcata(d, s); sink += d[0];
    for (i = 0; i < 4000; ++i) s[i] = (char)(0x41 + i % 26);
    s[4000] = 0;
    strcpy(d, "x"); sink += (long long)(size_t)wia_lstrcata(d, s); sink += d[0];
    for (i = 0; i < 4000; ++i) d[i] = (char)(0x61 + i % 23);
    d[4000] = 0;
    sink += (long long)(size_t)wia_lstrcata(d, "tail"); sink += d[0];
    sink += (long long)(size_t)wia_lstrcata(0, s);
    sink += (long long)(size_t)wia_lstrcata(d, 0);
    sink += (long long)(size_t)wia_lstrcata(0, 0);
}

#elif defined(T_230)
#define NAME "230-lstrcatw"
extern wchar_t* wia_lstrcatw(wchar_t*, const wchar_t*);
static void thunk(void){
    /* the same shapes wide, including ODD-ALIGNED destinations, which is where the split-character
       rule lives: the export writes whole characters only. */
    static wchar_t d[9000];
    static wchar_t s[5000];
    int i;
    wcscpy(s, L"APPENDED");
    wcscpy(d, L"");        sink += (long long)(size_t)wia_lstrcatw(d, s); sink += d[0];
    wcscpy(d, L"abcdefgh"); sink += (long long)(size_t)wia_lstrcatw(d, s); sink += d[0];
    wcscpy(d, L"0123456789012345678901234567890123456789");
    sink += (long long)(size_t)wia_lstrcatw(d, s); sink += d[0];
    sink += (long long)(size_t)wia_lstrcatw((wchar_t*)((char*)d + 1), s);  /* odd-aligned */
    for (i = 0; i < 4000; ++i) s[i] = (wchar_t)(0x41 + i % 26);
    s[4000] = 0;
    wcscpy(d, L"x"); sink += (long long)(size_t)wia_lstrcatw(d, s); sink += d[0];
    for (i = 0; i < 4000; ++i) d[i] = (wchar_t)(0x61 + i % 23);
    d[4000] = 0;
    sink += (long long)(size_t)wia_lstrcatw(d, L"tail"); sink += d[0];
    sink += (long long)(size_t)wia_lstrcatw(0, s);
    sink += (long long)(size_t)wia_lstrcatw(d, 0);
    sink += (long long)(size_t)wia_lstrcatw(0, 0);
}

#elif defined(T_241)
#define NAME "241-pathcchaddbackslashex"
extern long wia_pathcchaddbackslashex(wchar_t*, size_t, wchar_t**, size_t*);
extern long wia_pathcchremovebackslashex(wchar_t*, size_t, wchar_t**, size_t*);
static void thunk(void){
    /* both exports and every path: the appending and removing writes, both declines, the short-string
       vector fast path and the long loop, the two DIFFERENT cch ceilings (AddBackslashEx refuses above
       0x7FFFFFFF+n while RemoveBackslashEx has no ceiling at all), the too-small-cch failures -- which
       write the out-parameters too -- and both out-parameters NULL, which is a separate branch. */
    static wchar_t p[4200];
    wchar_t* e; size_t r;
    int i;
    wcscpy(p, L"C:\\dir\\file");
    sink += wia_pathcchaddbackslashex(p, 0x8000, &e, &r);      /* appends */
    sink += p[0] + (long long)r;
    wcscpy(p, L"C:\\dir\\");
    sink += wia_pathcchaddbackslashex(p, 0x8000, &e, &r);      /* declines: already ends in one */
    sink += p[0];
    wcscpy(p, L"");
    sink += wia_pathcchaddbackslashex(p, 0x8000, &e, &r);      /* the empty string */
    wcscpy(p, L"C:\\dir\\file");
    sink += wia_pathcchaddbackslashex(p, 4, &e, &r);           /* cch too small */
    wcscpy(p, L"C:\\dir\\file");
    sink += wia_pathcchaddbackslashex(p, 0, &e, &r);
    wcscpy(p, L"C:\\dir\\file");
    sink += wia_pathcchaddbackslashex(p, (size_t)-1, &e, &r);  /* past its moving ceiling */
    wcscpy(p, L"C:\\dir\\file");
    sink += wia_pathcchaddbackslashex(p, 0x8000, 0, 0);        /* both out-parameters NULL */
    wcscpy(p, L"C:\\dir\\file\\");
    sink += wia_pathcchremovebackslashex(p, 0x8000, &e, &r);   /* removes */
    sink += p[0] + (long long)r;
    wcscpy(p, L"C:\\dir\\file");
    sink += wia_pathcchremovebackslashex(p, 0x8000, &e, &r);   /* declines */
    sink += p[0];
    wcscpy(p, L"C:\\");
    sink += wia_pathcchremovebackslashex(p, 0x8000, &e, &r);   /* a protected root */
    sink += p[0];
    wcscpy(p, L"C:\\dir\\file\\");
    sink += wia_pathcchremovebackslashex(p, (size_t)-1, &e, &r); /* accepts SIZE_MAX: no ceiling */
    wcscpy(p, L"C:\\dir\\file\\");
    sink += wia_pathcchremovebackslashex(p, 2, &e, &r);        /* cch too small */
    wcscpy(p, L"C:\\dir\\file\\");
    sink += wia_pathcchremovebackslashex(p, 0x8000, 0, 0);     /* both out-parameters NULL */
    p[0] = 0x43; p[1] = 0x3A; p[2] = 0x5C;
    for (i = 3; i < 4000; ++i) p[i] = (i % 8 == 7) ? 0x5C : (wchar_t)(0x61 + i % 23);
    p[3999] = 0x61; p[4000] = 0;
    sink += wia_pathcchaddbackslashex(p, 0x8000, &e, &r);      /* the long 64-byte-per-iteration loop */
    sink += p[0];
    p[4000] = 0x5C; p[4001] = 0;
    sink += wia_pathcchremovebackslashex(p, 0x8000, &e, &r);
    sink += p[0];
}

#elif defined(T_242)
#define NAME "242-pathcchappendex"
extern long wia_pathcchappendex(wchar_t*, size_t, const wchar_t*, unsigned long);
extern long wia_pathcchcombineex(wchar_t*, size_t, const wchar_t*, const wchar_t*, unsigned long);
static void thunk(void){
    /* both exports, and every branch of the join: an ordinary seam, a `more` that replaces, a drive
       and a UNC `more`, the "\\?" exception that does NOT replace, Combine's rooted case and its
       refusal, the extended-prefix forms on both sides, a prefix that STRADDLES the seam, a pop that
       reaches back into the base's output, the AVX2 fast path at length, both length caps and a
       too-small cch. dwFlags stays 0, which is the implemented domain; a nonzero value tail-jumps to a
       fallback this driver does not install. */
    static wchar_t p[1200];
    static wchar_t out[1200];
    int i;
    wcscpy(p, L"C:\\dir");
    sink += wia_pathcchappendex(p, 0x8000, L"file.txt", 0);   /* the ordinary seam */
    sink += p[0];
    wcscpy(p, L"C:\\dir\\");
    sink += wia_pathcchappendex(p, 0x8000, L"\\file", 0);     /* the separator stripped at the seam */
    sink += p[0];
    wcscpy(p, L"C:\\dir");
    sink += wia_pathcchappendex(p, 0x8000, L"..\\up", 0);     /* a pop into the base's output */
    sink += p[0];
    wcscpy(p, L"C:\\dir");
    sink += wia_pathcchappendex(p, 0x8000, L"D:\\x", 0);      /* a drive `more` replaces */
    sink += p[0];
    wcscpy(p, L"C:\\dir");
    sink += wia_pathcchappendex(p, 0x8000, L"\\\\srv\\shr", 0); /* a UNC `more` replaces */
    sink += p[0];
    wcscpy(p, L"a");
    sink += wia_pathcchappendex(p, 0x8000, L"\\\\?", 0);      /* "\\?" does NOT replace */
    sink += p[0];
    wcscpy(p, L"\\\\?");
    sink += wia_pathcchappendex(p, 0x8000, L"C:", 0);         /* the prefix STRADDLES the seam */
    sink += p[0];
    wcscpy(p, L"");
    sink += wia_pathcchappendex(p, 0x8000, L"\\a", 0);        /* an empty base */
    sink += p[0];
    wcscpy(p, L"C:\\dir");
    sink += wia_pathcchappendex(p, 0x8000, 0, 0);             /* a NULL `more` reads as empty */
    sink += wia_pathcchappendex(0, 0x8000, L"x", 0);          /* a NULL destination is refused */
    wcscpy(p, L"C:\\dir");
    sink += wia_pathcchappendex(p, 4, L"file", 0);            /* cch too small */
    wcscpy(p, L"C:\\dir");
    sink += wia_pathcchappendex(p, 0, L"file", 0);            /* cch 0 */
    wcscpy(p, L"C:\\dir");
    sink += wia_pathcchappendex(p, 0x8001, L"file", 0);       /* past the maximum */
    p[0] = 0x43; p[1] = 0x3A; p[2] = 0x5C;
    for (i = 3; i < 250; ++i) p[i] = (i % 8 == 7) ? 0x5C : (wchar_t)(0x61 + i % 23);
    p[250] = 0;
    sink += wia_pathcchappendex(p, 0x8000, L"z", 0);          /* the vectorised fast path */
    sink += p[0];
    for (i = 3; i < 400; ++i) p[i] = (i % 8 == 7) ? 0x5C : (wchar_t)(0x61 + i % 23);
    p[400] = 0;
    sink += wia_pathcchappendex(p, 0x8000, L"z", 0);          /* past the result cap */

    sink += wia_pathcchcombineex(out, 0x8000, L"C:\\dir", L"file", 0);
    sink += out[0];
    sink += wia_pathcchcombineex(out, 0x8000, L"C:\\dir\\sub", L"\\rooted", 0);  /* the rooted case */
    sink += out[0];
    sink += wia_pathcchcombineex(out, 0x8000, L"\\\\srv\\shr\\x", L"\\y", 0);    /* a UNC root */
    sink += out[0];
    sink += wia_pathcchcombineex(out, 0x8000, L"\\\\?\\C:\\x", L"\\y", 0);       /* a prefixed root */
    sink += out[0];
    sink += wia_pathcchcombineex(out, 0x8000, L"\\\\?\\UNC\\s\\h", L"\\y", 0);
    sink += out[0];
    sink += wia_pathcchcombineex(out, 0x8000, L"relative", L"\\y", 0);           /* refused */
    sink += wia_pathcchcombineex(out, 0x8000, L"\\\\?", L"\\y", 0);              /* also refused */
    sink += wia_pathcchcombineex(out, 0x8000, 0, L"y", 0);                       /* a NULL base */
    sink += out[0];
    sink += wia_pathcchcombineex(out, 0x8000, L"C:\\a", 0, 0);                   /* a NULL `more` */
    sink += out[0];
    sink += wia_pathcchcombineex(out, 0x8000, 0, 0, 0);                          /* both NULL */
    sink += wia_pathcchcombineex(0, 0x8000, L"C:\\a", L"b", 0);                  /* a NULL output */
    sink += wia_pathcchcombineex(out, 6, L"C:\\a\\b", L"c", 0);                  /* cch too small */
    sink += wia_pathcchcombineex(out, 0x8000, p, L"z", 0);                       /* past the cap */
    sink += out[0];
}

#elif defined(T_243)
#define NAME "243-pathcchcanonicalizeex"
extern long wia_pathcchcanonicalizeex(wchar_t*, size_t, const wchar_t*, unsigned long);
extern void wia_pccx_set_fallback(void*);
static void thunk(void){
    /* every path: the AVX2 fast path at several lengths, the per-component walk that a dot component
       forces, a pop that empties the output, a pop refused by PathCchIsRoot, the two prefix forms,
       the trailing-dot finish, the drive-root fixup, both length caps, and a too-small cch. dwFlags
       stays 0 throughout, which is the implemented domain -- a nonzero value tail-jumps to the
       fallback, and this driver does not install one. */
    static wchar_t out[1200];
    static wchar_t in[1200];
    int i;
    sink += wia_pathcchcanonicalizeex(out, 0x8000, L"C:\\dir\\file.txt", 0);
    sink += out[0];
    sink += wia_pathcchcanonicalizeex(out, 0x8000, L"C:\\a\\..\\b", 0);       /* a pop */
    sink += out[0];
    sink += wia_pathcchcanonicalizeex(out, 0x8000, L"a\\..\\b", 0);          /* a pop to empty */
    sink += out[0];
    sink += wia_pathcchcanonicalizeex(out, 0x8000, L"C:\\..", 0);            /* refused by isroot */
    sink += out[0];
    sink += wia_pathcchcanonicalizeex(out, 0x8000, L"\\\\srv\\shr\\a\\..\\..", 0);
    sink += out[0];
    sink += wia_pathcchcanonicalizeex(out, 0x8000, L"\\\\?\\C:\\a\\.\\b", 0); /* the drive prefix */
    sink += out[0];
    sink += wia_pathcchcanonicalizeex(out, 0x8000, L"\\\\?\\UNC\\s\\h\\..", 0); /* the UNC prefix */
    sink += out[0];
    sink += wia_pathcchcanonicalizeex(out, 0x8000, L"C:\\z..", 0);           /* the trailing strip */
    sink += out[0];
    sink += wia_pathcchcanonicalizeex(out, 0x8000, L"C:", 0);                /* the drive fixup */
    sink += out[0];
    sink += wia_pathcchcanonicalizeex(out, 0x8000, L"", 0);                  /* the empty fixup */
    sink += out[0];
    sink += wia_pathcchcanonicalizeex(out, 1, L"", 0);                       /* the fixup skipped */
    sink += wia_pathcchcanonicalizeex(out, 8, L"C:\\a\\b\\c\\d\\e", 0);       /* cch too small */
    sink += wia_pathcchcanonicalizeex(out, 0, L"C:\\a", 0);                  /* cch 0 */
    sink += wia_pathcchcanonicalizeex(out, 0x8001, L"C:\\a", 0);             /* past the maximum */
    in[0] = 0x43; in[1] = 0x3A; in[2] = 0x5C;
    for (i = 3; i < 250; ++i) in[i] = (i % 8 == 7) ? 0x5C : (wchar_t)(0x61 + i % 23);
    in[250] = 0;
    sink += wia_pathcchcanonicalizeex(out, 0x8000, in, 0);                   /* the vector fast path */
    sink += out[0];
    for (i = 3; i < 300; ++i) in[i] = (wchar_t)(0x61 + i % 23);              /* one long component */
    in[300] = 0;
    sink += wia_pathcchcanonicalizeex(out, 0x8000, in, 0);
    for (i = 3; i < 400; ++i) in[i] = (i % 8 == 7) ? 0x5C : (wchar_t)(0x61 + i % 23);
    in[400] = 0;
    sink += wia_pathcchcanonicalizeex(out, 0x8000, in, 0);                   /* past the result cap */
    sink += out[0];
}

#elif defined(T_240)
#define NAME "240-pathcchremovefilespec"
extern long wia_pathcchremovefilespec(wchar_t*, size_t);
static void thunk(void){
    /* every branch: a plain cut, a UNC root, the extended prefix, a no-op that writes nothing, a
       too-small cch rejected before any scanning, NULL, and a 1000-character path so the wcslen
       and the backward scan both run their vector loops. */
    static wchar_t p[1200];
    int i;
    wcscpy(p, L"C:\\\\dir\\\\file.txt");
    sink += wia_pathcchremovefilespec(p, 0x8000);
    sink += p[0];
    wcscpy(p, L"\\\\\\\\srv\\\\shr\\\\a");
    sink += wia_pathcchremovefilespec(p, 0x8000);   /* a UNC root */
    sink += p[0];
    wcscpy(p, L"\\\\\\\\?\\\\C:\\\\dir\\\\f");
    sink += wia_pathcchremovefilespec(p, 0x8000);   /* the extended prefix */
    sink += p[0];
    wcscpy(p, L"\\\\\\\\srv\\\\shr");
    sink += wia_pathcchremovefilespec(p, 0x8000);   /* its own root: writes nothing */
    sink += p[0];
    wcscpy(p, L"C:\\\\dir\\\\file.txt");
    sink += wia_pathcchremovefilespec(p, 2);        /* rejected before any scan */
    sink += wia_pathcchremovefilespec(0, 0x8000);   /* NULL */
    sink += wia_pathcchremovefilespec(p, 0);        /* cch 0 */
    sink += wia_pathcchremovefilespec(p, 0x8001);   /* past PATHCCH_MAX_CCH */
    p[0] = 0x43; p[1] = 0x3A; p[2] = 0x5C;
    for (i = 3; i < 1000; ++i) p[i] = (i % 8 == 7) ? 0x5C : (wchar_t)(0x61 + i % 23);
    p[1000] = 0;
    sink += wia_pathcchremovefilespec(p, 0x8000);   /* the long scan */
    sink += p[0];
}

#elif defined(T_218)
#define NAME "218-strtrima"
extern int wia_strtrima(char*, const char*);
static void thunk(void){
    /* a trim at both ends (the move), one that trims nothing, an all-trim string, a set spanning
       BOTH halves of the bitmap, and both NULL exits. The set-building loop writes into the
       CALLER'S SHADOW SPACE, so this also proves that leaves the caller's frame intact. */
    static char p[64];
    memcpy(p, "AAhello worldAA", 18);
    sink += wia_strtrima(p, "A");
    memcpy(p, "hello world", 12);
    sink += wia_strtrima(p, "A");
    memcpy(p, "AAAA", 5);
    sink += wia_strtrima(p, "A");
    memcpy(p, "ÃzzÃ", 5);
    sink += wia_strtrima(p, "AÃ");
    sink += wia_strtrima(p, 0);
    sink += wia_strtrima(0, "A");
    sink += p[0];
}

#elif defined(T_219)
#define NAME "219-pathstrippatha"
extern void wia_pathstrippatha(char*);
static void thunk(void){
    static char p[128];
    memcpy(p, "C:\\some\\reasonably\\long\\path\\to\\a\\file.txt", 42);
    wia_pathstrippatha(p);                 /* a long move */
    sink += p[0];
    memcpy(p, "already-stripped.txt", 21);
    wia_pathstrippatha(p);                 /* no move at all */
    sink += p[0];
    wia_pathstrippatha(0);
}

#elif defined(T_220)
#define NAME "220-strchra"
extern const char* wia_strchra(const char*, WORD);
static void thunk(void){
    sink += (long long)(size_t)wia_strchra(a8, (WORD)'Z');      /* a hit */
    sink += (long long)(size_t)wia_strchra(a8, (WORD)'#');      /* a full-scan miss */
    sink += (long long)(size_t)wia_strchra(a8, (WORD)0x5A00);   /* low byte NUL -> NULL */
    sink += (long long)(size_t)wia_strchra(0, (WORD)'Z');
}

#elif defined(T_221)
#define NAME "221-pathremoveblanksa"
extern void wia_pathremoveblanksa(char*);
static void thunk(void){
    static char p[128];
    memcpy(p, "   C:\\some\\long\\path\\to\\a\\file.txt   ", 39);
    wia_pathremoveblanksa(p);          /* both ends: the move AND the trailing cut */
    sink += p[0];
    memcpy(p, "nothing-to-strip", 17);
    wia_pathremoveblanksa(p);          /* writes nothing at all */
    sink += p[0];
    memcpy(p, "      ", 7);
    wia_pathremoveblanksa(p);          /* entirely blanks */
    sink += p[0];
    wia_pathremoveblanksa(0);
}

#elif defined(T_222)
#define NAME "222-pathremoveextensiona"
extern void wia_pathremoveexta(char*);
static void thunk(void){
    static char p[512];
    memcpy(p, "C:\\some\\path\\to\\a\\file.txt", 27);
    wia_pathremoveexta(p);                 /* an ordinary removal */
    sink += p[0];
    memcpy(p, "no-extension-here", 18);
    wia_pathremoveexta(p);                 /* writes nothing */
    sink += p[0];
    { int i; for (i = 0; i < 300; ++i) p[i] = 'a'; p[280] = '.'; p[300] = 0; }
    wia_pathremoveexta(p);                 /* past the MAX_PATH guard: writes nothing */
    sink += p[0];
    wia_pathremoveexta(0);
}

#elif defined(T_244)
#define NAME "244-hashdata"
extern long wia_hashdata(const unsigned char*, unsigned long, unsigned char*, unsigned long);
static void thunk(void){
    /* every path this implementation has, because they use different register sets: the four LEAF
       kernels (cbHash 1..4, which save nothing at all), the framed twelve-lane kernel, its
       four-lane last group, a digest long enough to need many passes, the seed-only path with both
       its vector blocks and its byte tail, the OVERLAP fallback -- which is the only path that
       writes the digest through memory -- and every NULL combination. */
    static unsigned char s[5000];
    static unsigned char d[1100];
    static unsigned char both[512];
    int i;
    for (i = 0; i < 5000; ++i) s[i] = (unsigned char)(i * 31 + 7);
    sink += wia_hashdata(s, 137, d, 1);      sink += d[0];   /* leaf, one lane */
    sink += wia_hashdata(s, 137, d, 2);      sink += d[1];   /* leaf, two lanes */
    sink += wia_hashdata(s, 137, d, 3);      sink += d[2];   /* leaf, three lanes */
    sink += wia_hashdata(s, 137, d, 4);      sink += d[3];   /* leaf, four lanes */
    sink += wia_hashdata(s, 137, d, 5);      sink += d[4];   /* framed, one partial group */
    sink += wia_hashdata(s, 137, d, 12);     sink += d[11];  /* framed, exactly one group */
    sink += wia_hashdata(s, 4000, d, 16);    sink += d[15];  /* twelve lanes then four */
    sink += wia_hashdata(s, 37, d, 1000);    sink += d[999]; /* many passes */
    sink += wia_hashdata(s, 0, d, 1000);     sink += d[999]; /* the seed alone: vector + tail */
    sink += wia_hashdata(s, 137, d, 0);                      /* writes nothing */
    for (i = 0; i < 512; ++i) both[i] = (unsigned char)(i * 37 + 11);
    sink += wia_hashdata(both + 8, 24, both, 20);            /* OVERLAPPING: the fallback */
    sink += both[0];
    sink += wia_hashdata(0, 4, d, 4);
    sink += wia_hashdata(s, 4, 0, 4);
    sink += wia_hashdata(0, 0, 0, 0);
}

#elif defined(T_245)
#define NAME "245-urlunescapew"
extern long wia_urlunescapew(wchar_t*, wchar_t*, unsigned long*, unsigned long);
extern void wia_uue_set_fallback(void*);
/* the delegated flag domain tail-jumps to the shipped export, so it has to be installed before the
   thunk runs -- and the tail jump is itself the thing worth checking here, because it happens with
   eight non-volatile registers already pushed and must unwind them all before transferring */
#define SETUP() wia_uue_set_fallback((void*)GetProcAddress(LoadLibraryW(L"shlwapi.dll"), \
                                                          "UrlUnescapeW"))
static void thunk(void){
    /* every path, because they use different register sets and two of them leave through a tail
       jump: the fast path (buffer bigger than the input, so only the %00 pattern scan runs), the
       measuring path (buffer smaller, so the full walk runs), E_POINTER, the %00 refusal, the
       escape-dense tight loop, a long plain run through the 32-byte copy ladder, the extra-info
       flag, in-place, the DELEGATED flag domain, and every NULL combination. */
    static wchar_t in[4200];
    static wchar_t out[4200];
    unsigned long cch;
    int i;
    wcscpy(in, L"a%41b%42c");
    cch = 64;  sink += wia_urlunescapew(in, out, &cch, 0);            sink += out[0] + cch;
    cch = 4;   sink += wia_urlunescapew(in, out, &cch, 0);            sink += cch;  /* E_POINTER */
    cch = 6;   sink += wia_urlunescapew(in, out, &cch, 0);            sink += cch;  /* measuring */
    wcscpy(in, L"a%00b");
    cch = 64;  sink += wia_urlunescapew(in, out, &cch, 0);            sink += cch;  /* refusal */
    for (i = 0; i < 1200; ++i) in[i] = (wchar_t)(0x61 + i % 23);
    in[1200] = 0;
    cch = 2000; sink += wia_urlunescapew(in, out, &cch, 0);           sink += out[0] + cch;
    for (i = 0; i < 1200; i += 3) { in[i] = L'%'; in[i+1] = L'4'; in[i+2] = L'1'; }
    in[1200] = 0;
    cch = 2000; sink += wia_urlunescapew(in, out, &cch, 0);           sink += out[0] + cch;
    wcscpy(in, L"a%41b?c%42d#e%43f");
    cch = 64;  sink += wia_urlunescapew(in, out, &cch, 0x02000000);   sink += out[0] + cch;
    cch = 64;  sink += wia_urlunescapew(in, out, &cch, 0x00040000);   sink += cch;  /* DELEGATED */
    wcscpy(in, L"a%41b%42c");
    cch = 0;   sink += wia_urlunescapew(in, 0, &cch, 0x00100000);     sink += in[0];  /* in place */
    cch = 64;  sink += wia_urlunescapew(0, out, &cch, 0);
    cch = 64;  sink += wia_urlunescapew(in, 0, &cch, 0);
    sink += wia_urlunescapew(in, out, 0, 0);
    cch = 0;   sink += wia_urlunescapew(in, out, &cch, 0);
}

#elif defined(T_246)
#define NAME "246-pathcanonicalizew"
extern int  wia_pathcanonicalizew(wchar_t*, const wchar_t*);
extern void wia_pccx_set_fallback(void*);
/* the envelope always passes dwFlags = 0, so change 243's delegation pointer is never followed --
   it is installed so that a bug which passed something else would fail here rather than jump
   through a null pointer */
#define SETUP() wia_pccx_set_fallback((void*)GetProcAddress(LoadLibraryW(L"kernelbase.dll"), \
                                                           "PathCchCanonicalizeEx"))
static void thunk(void){
    /* the envelope's own paths -- both NULL checks, the TRUE return, and both halves of the failure
       mapping -- plus enough of change 243's core to make sure the call through it preserves
       everything: a plain path, a dot-dot walk, the MAX_PATH cap on both sides, and a path whose
       canonical form is far shorter than its input. */
    static wchar_t out[600];
    static wchar_t in[700];
    int i, k;
    sink += wia_pathcanonicalizew(out, L"C:\\dir\\file.txt");      sink += out[0];
    sink += wia_pathcanonicalizew(out, L"C:\\a\\..\\b");           sink += out[0];
    sink += wia_pathcanonicalizew(out, L"\\\\srv\\shr\\x\\..");    sink += out[0];
    sink += wia_pathcanonicalizew(out, L"\\\\?\\C:\\a\\..\\b");    sink += out[0];
    sink += wia_pathcanonicalizew(out, L"");                       sink += out[0];
    for (i = 0; i < 259; ++i) in[i] = (i % 9 == 8) ? 0x5C : (wchar_t)(0x61 + i % 23);
    in[0] = 0x43; in[1] = 0x3A; in[2] = 0x5C; in[259] = 0;
    sink += wia_pathcanonicalizew(out, in);                        sink += out[0];  /* 259: fits */
    for (i = 0; i < 260; ++i) in[i] = (i % 9 == 8) ? 0x5C : (wchar_t)(0x61 + i % 23);
    in[0] = 0x43; in[1] = 0x3A; in[2] = 0x5C; in[260] = 0;
    sink += wia_pathcanonicalizew(out, in);                        sink += out[0];  /* 260: refused */
    k = 0; in[k++] = 0x43; in[k++] = 0x3A; in[k++] = 0x5C;
    while (k < 594) { in[k++] = 0x61; in[k++] = 0x5C; in[k++] = 0x2E; in[k++] = 0x2E; in[k++] = 0x5C; }
    while (k < 600) in[k++] = 0x62;
    in[600] = 0;
    sink += wia_pathcanonicalizew(out, in);                        sink += out[0];
    sink += wia_pathcanonicalizew(out, 0);                         sink += out[0];  /* clears out[0] */
    sink += wia_pathcanonicalizew(0, L"C:\\a");
    sink += wia_pathcanonicalizew(0, 0);
}

#elif defined(T_247)
#define NAME "247-pathaddextensionw"
extern int wia_pathaddextensionw(wchar_t*, const wchar_t*);
static void thunk(void){
    /* every path: the append, the two refusals (already has an extension, and the result would
       exceed MAX_PATH), the NULL default extension -- which is L".exe", not empty -- an empty
       extension that writes nothing, the boundary at exactly 259 characters of result, and the
       NULL-path cases. */
    static wchar_t p[700];
    int i;
    wcscpy(p, L"C:\\dir\\file");
    sink += wia_pathaddextensionw(p, L".abc");        sink += p[0];   /* appends */
    sink += wia_pathaddextensionw(p, L".abc");        sink += p[0];   /* now has one: refuses */
    wcscpy(p, L"C:\\dir\\file");
    sink += wia_pathaddextensionw(p, 0);              sink += p[0];   /* the .exe default */
    wcscpy(p, L"C:\\dir\\file");
    sink += wia_pathaddextensionw(p, L"");            sink += p[0];   /* writes nothing, TRUE */
    p[0] = 0;
    sink += wia_pathaddextensionw(p, L".exe");        sink += p[0];   /* the empty path */
    for (i = 0; i < 255; ++i) p[i] = (i % 9 == 8) ? 0x5C : (wchar_t)(0x61 + i % 23);
    p[0] = 0x43; p[1] = 0x3A; p[2] = 0x5C; p[255] = 0;
    sink += wia_pathaddextensionw(p, L".abc");        sink += p[0];   /* 255 + 4 = 259: fits */
    for (i = 0; i < 256; ++i) p[i] = (i % 9 == 8) ? 0x5C : (wchar_t)(0x61 + i % 23);
    p[0] = 0x43; p[1] = 0x3A; p[2] = 0x5C; p[256] = 0;
    sink += wia_pathaddextensionw(p, L".abc");        sink += p[0];   /* 256 + 4 = 260: refused */
    for (i = 0; i < 600; ++i) p[i] = (i % 9 == 8) ? 0x5C : (wchar_t)(0x61 + i % 23);
    p[0] = 0x43; p[1] = 0x3A; p[2] = 0x5C; p[600] = 0;
    sink += wia_pathaddextensionw(p, L".abc");        sink += p[0];   /* long: refused */
    sink += wia_pathaddextensionw(0, L".abc");
    sink += wia_pathaddextensionw(0, 0);
}

#elif defined(T_248)
#define NAME "248-urlunescapea"
extern long wia_urlunescapea(char*, char*, unsigned long*, unsigned long);
static void thunk(void){
    /* FOUR kernels and a compiled C envelope, and the envelope is the reason this case matters: the
       fault path unwinds THROUGH it out of hand-written assembly, and an unwind that restored the
       wrong registers -- or left the upper YMM halves dirty -- is invisible to correctness, because
       the answer is S_OK with an empty result either way. So the last block here faults on purpose.
       Before it: the one-pass path (buffer bigger than the source), the TWO-pass path (buffer
       smaller, so the measuring kernel runs as well), E_POINTER, the AS_UTF8 refusal, %00 truncating
       on one path and refusing on the other, the escape-dense inner loop, a long plain run through
       the 32-byte copy ladder, the masked remainder at 63 bytes, the extra-info flag, the STAGED
       overlap path (which allocates), in place, and every NULL combination. */
    static char in[4200];
    static char out[4200];
    static char ov[4200];
    unsigned long cch;
    int i;
    strcpy(in, "a%41b%42c");
    cch = 64;  sink += wia_urlunescapea(in, out, &cch, 0);            sink += out[0] + cch;
    cch = 3;   sink += wia_urlunescapea(in, out, &cch, 0);            sink += cch;  /* E_POINTER */
    cch = 6;   sink += wia_urlunescapea(in, out, &cch, 0);            sink += cch;  /* two passes */
    cch = 64;  sink += wia_urlunescapea(in, out, &cch, 0x00040000);   sink += cch;  /* AS_UTF8 */
    strcpy(in, "a%00b");
    cch = 64;  sink += wia_urlunescapea(in, out, &cch, 0);            sink += cch;  /* truncates */
    cch = 0;   sink += wia_urlunescapea(in, 0, &cch, 0x00100000);     sink += in[0];  /* refuses */
    for (i = 0; i < 63; ++i) in[i] = (char)(0x61 + i % 23);
    in[63] = 0;
    cch = 200; sink += wia_urlunescapea(in, out, &cch, 0);            sink += out[0] + cch;
    for (i = 0; i < 1200; ++i) in[i] = (char)(0x61 + i % 23);
    in[1200] = 0;
    cch = 2000; sink += wia_urlunescapea(in, out, &cch, 0);           sink += out[0] + cch;
    for (i = 0; i < 1200; i += 3) { in[i] = '%'; in[i+1] = '4'; in[i+2] = '1'; }
    in[1200] = 0;
    cch = 2000; sink += wia_urlunescapea(in, out, &cch, 0);           sink += out[0] + cch;
    cch = 401;  sink += wia_urlunescapea(in, out, &cch, 0);           sink += out[0] + cch;
    strcpy(in, "a%41b?c%42d#e%43f");
    cch = 64;  sink += wia_urlunescapea(in, out, &cch, 0x02000000);   sink += out[0] + cch;
    /* the STAGED path: a destination above the source and inside it, which the kernels cannot write
       forward and the envelope therefore copies aside first -- twice, once short enough for the
       inline buffer and once long enough to reach the heap */
    strcpy(ov, "a%41b%42c%43d%44e");
    cch = 64;  sink += wia_urlunescapea(ov, ov + 2, &cch, 0);         sink += ov[2] + cch;
    for (i = 0; i < 2000; ++i) ov[i] = (i % 3 == 0) ? '%' : (i % 3 == 1) ? '4' : '1';
    ov[2000] = 0;
    cch = 3000; sink += wia_urlunescapea(ov, ov + 8, &cch, 0);        sink += ov[8] + cch;
    strcpy(in, "a%41b%42c");
    cch = 0;   sink += wia_urlunescapea(in, 0, &cch, 0x00100000);     sink += in[0];  /* in place */
    cch = 64;  sink += wia_urlunescapea(0, out, &cch, 0);
    cch = 64;  sink += wia_urlunescapea(in, 0, &cch, 0);
    sink += wia_urlunescapea(in, out, 0, 0);
    cch = 0;   sink += wia_urlunescapea(in, out, &cch, 0);
    /* AND THE FAULT. An unterminated source at a PAGE_NOACCESS page: lstrlenA swallows it, so the
       shipped function returns S_OK with an empty result, and so must this -- by unwinding out of
       the assembly scan and through the C __except. That is the path this gate exists for. */
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        {
            char* g = (char*)VirtualAlloc(0, si.dwPageSize * 2,
                                          MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (g) {
                unsigned long old;
                char* s2 = (g + si.dwPageSize) - 4;
                VirtualProtect(g + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old);
                for (i = 0; i < 4; ++i) s2[i] = (char)('a' + i);
                cch = 64;
                sink += wia_urlunescapea(s2, out, &cch, 0);           sink += out[0] + cch;
                VirtualFree(g, 0, MEM_RELEASE);
            }
        }
    }
}

#elif defined(T_249)
#define NAME "249-urlhasha"
extern long wia_urlhasha(const char*, unsigned char*, unsigned long);
static void thunk(void){
    /* An ENVELOPE over two other changes, so what this case is really checking is the SEAM: six
       instructions that keep the url, the digest and cbHash in rbx, rsi and rdi across a call to
       change 225's SEH-wrapped length and then a call to change 244's kernel. Every one of those
       three is non-volatile, so a callee that failed to preserve them would corrupt arguments this
       envelope still needs -- and 244 reaches for xmm registers on its seed path.
       Driven: both NULL refusals; cbHash 0, which writes nothing; the four LEAF kernels (1..4); the
       twelve-lane kernel and its second pass (12, 13, 16); a digest bigger than 256, where the seed
       WRAPS; an OVERLAPPING digest, which is the byte-for-byte fallback; a long url; and a url that
       FAULTS, because the swallow unwinds out of assembly through change 225's C __except. */
    static char u[4200];
    static unsigned char d[600];
    int i;
    strcpy(u, "http://example.com/a/b?c=d");
    for (i = 1; i <= 4; ++i)  { sink += wia_urlhasha(u, d, i);  sink += d[0]; }
    sink += wia_urlhasha(u, d, 5);   sink += d[0];
    sink += wia_urlhasha(u, d, 12);  sink += d[0];
    sink += wia_urlhasha(u, d, 13);  sink += d[0];
    sink += wia_urlhasha(u, d, 16);  sink += d[0];
    sink += wia_urlhasha(u, d, 0);   sink += d[0];
    sink += wia_urlhasha(u, d, 300); sink += d[299];          /* the seed wraps at 256 */
    sink += wia_urlhasha("", d, 16); sink += d[0];
    for (i = 0; i < 4000; ++i) u[i] = (char)('a' + i % 26);
    u[4000] = 0;
    sink += wia_urlhasha(u, d, 16);  sink += d[0];
    sink += wia_urlhasha(u, d, 1);   sink += d[0];
    /* the digest INSIDE the url: change 244's grouped kernel is wrong on every overlapping
       placement and hands them to a byte-for-byte emulation of the shipped loop */
    strcpy(u, "abcdefghijklmnopqrstuvwxyz");
    sink += wia_urlhasha(u, (unsigned char*)u + 4, 12);  sink += u[4];
    sink += wia_urlhasha(0, d, 16);
    sink += wia_urlhasha("abc", 0, 16);
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        {
            char* g = (char*)VirtualAlloc(0, si.dwPageSize * 2,
                                          MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (g) {
                unsigned long old;
                char* s2 = (g + si.dwPageSize) - 6;
                VirtualProtect(g + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old);
                for (i = 0; i < 6; ++i) s2[i] = (char)('a' + i);   /* NO terminator */
                sink += wia_urlhasha(s2, d, 16);  sink += d[0];
                VirtualFree(g, 0, MEM_RELEASE);
            }
        }
    }
}

#elif defined(T_250)
#define NAME "250-rtlipv6stringtoaddressexw"
extern long wia_ip6exw(const wchar_t*, void*, unsigned long*, unsigned short*);
static void thunk(void){
    /* An ENVELOPE over change 166, so the seam is what this checks: the cursor lives in rsi across a
       call into a routine that saves seven registers of its own, and ScopeId, Port and the bracket
       flag live in this frame's slots rather than in saved registers -- a callee that failed to
       restore rsi, or an unwind descriptor that did not match the 64-byte allocation, would corrupt
       a parse that still looked plausible.
       Driven: all four NULL refusals; the three port bases and an empty body at each; the scope at
       its cap and one past; the port at its cap and one past; a bracket opened and never closed and
       one closed without opening; an address whose remainder the W form would report and this form
       must reject; and change 166's own paths underneath -- "::" (nothing after the compression, the
       skip added while landing this change), a full eight groups, and an embedded IPv4 tail. */
    static wchar_t u[160];
    unsigned char a[16];
    unsigned long sc;
    unsigned short po;
    static const wchar_t* T[] = {
        L"::", L"::1", L"1:2:3:4:5:6:7:8", L"::ffff:1.2.3.4", L"1::",
        L"[::1]", L"[::1]:0", L"[::1]:80", L"[::1]:65535", L"[::1]:65536",
        L"[::1]:0x", L"[::1]:0x50", L"[::1]:0xffff", L"[::1]:0x10000",
        L"[::1]:0", L"[::1]:010", L"[::1]:0177777", L"[::1]:0200000", L"[::1]:",
        L"::1%0", L"::1%4294967295", L"::1%4294967296", L"::1%", L"[fe80::1%9]:443",
        L"[::1", L"::1]", L"[::1]80", L"::0x1", L"::1.2.3.0x5", L"", L"[", L"]",
    };
    int i;
    for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
        int k;
        for (k = 0; T[i][k]; ++k) u[k] = T[i][k];
        u[k] = 0;
        sc = 0xDEADBEEF; po = 0xBEEF;
        sink += wia_ip6exw(u, a, &sc, &po);
        sink += a[0] + sc + po;
    }
    sink += wia_ip6exw(0, a, &sc, &po);
    sink += wia_ip6exw(L"::1", 0, &sc, &po);
    sink += wia_ip6exw(L"::1", a, 0, &po);
    sink += wia_ip6exw(L"::1", a, &sc, 0);
}

#elif defined(T_167)
#define NAME "167-pathcommonprefixw"
extern int  wia_pathcommonprefixw(const wchar_t*, const wchar_t*, wchar_t*);
extern void wia_upcase_init(void);
#define SETUP() wia_upcase_init()
static void thunk(void){
    /* The vector loop uses ymm0-ymm5 and nothing above, which is the whole point of the register
       budget here -- xmm6-xmm15 are non-volatile in Win64 and an implementation that reached for
       ymm6 would return exactly the right answer at exactly the right speed and corrupt only a
       caller that happened to have a live double. Sixteen implementations in this repository did
       that undetected until change 202's benchmark caught one.
       Driven: both NULL refusals; the UNC skips and the UNC/non-UNC refusal; a boundary at the
       terminator and mid-component; the length-2 case that reports 3; a case-differing path, which
       is the only input that folds a block; a path long enough for many blocks; and a 260+ result,
       which copies nothing. */
    static wchar_t a[600], b[600], o[600];
    int i;
    static const wchar_t* T[][2] = {
        { L"C:\\a\\b\\c", L"C:\\a\\b\\d" }, { L"C:\\a", L"C:\\a" },
        { L"C:", L"C:" }, { L"\\a", L"\\a\\" }, { L"ab", L"ab\\c" },
        { L"\\\\srv\\share\\x", L"\\\\SRV\\SHARE\\y" },
        { L"\\\\srv\\s", L"C:\\s" }, { L"C:\\s", L"\\\\srv\\s" },
        { L"\\\\", L"\\\\\\" }, { L"\\", L"\\\\" },
        { L"", L"" }, { L"a", L"b" }, { L"C:/a/b", L"C:/a/c" },
    };
    for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i) {
        sink += wia_pathcommonprefixw(T[i][0], T[i][1], o);   sink += o[0];
        sink += wia_pathcommonprefixw(T[i][0], T[i][1], 0);
    }
    sink += wia_pathcommonprefixw(0, L"C:\\a", o);
    sink += wia_pathcommonprefixw(L"C:\\a", 0, o);
    /* long, identical -- many whole blocks */
    for (i = 0; i < 500; ++i) { a[i] = (i % 9 == 8) ? L'\\' : (wchar_t)(L'a' + i % 26);
                                b[i] = a[i]; }
    a[500] = 0; b[500] = 0;
    sink += wia_pathcommonprefixw(a, b, o);   sink += o[0];    /* result >= 260: copies nothing */
    /* long, differing only in case -- the block-fold path on every block */
    for (i = 0; i < 300; ++i) b[i] = (a[i] == L'\\') ? a[i] : (wchar_t)(a[i] - 32);
    b[300] = 0; a[300] = 0;
    sink += wia_pathcommonprefixw(a, b, o);   sink += o[0];
    /* and a non-ASCII pair, which the block fold gets WRONG and the table then settles */
    a[0] = 0x00E0; a[1] = 0; b[0] = 0x00C0; b[1] = 0;
    sink += wia_pathcommonprefixw(a, b, o);   sink += o[0];
}

#elif defined(T_177)
#define NAME "177-pathisprefixw"
extern int  wia_pathisprefixw(const wchar_t*, const wchar_t*);
extern void wia_upcase_init(void);
#define SETUP() wia_upcase_init()
static void thunk(void){
    /* An envelope over TWO landed changes -- 167 for the walk, 001 for the length -- so what this
       case checks is the seam across two calls: pszPath has to survive the first (it is passed to
       the second) and the length has to survive the second. Both live in non-volatile registers,
       and 167 reaches for ymm0-ymm5 while 001 reaches for its own.
       Driven: both NULL refusals, a true prefix, a false one, the trailing-separator case that does
       all the work and still returns FALSE, an empty prefix, a case-differing prefix (which is the
       only input that makes 167 fold a block), and a long path. */
    static wchar_t a[600], b[600];
    int i;
    static const wchar_t* T[][2] = {
        { L"C:\\a", L"C:\\a\\b" }, { L"C:\\a\\", L"C:\\a\\b" },
        { L"C:\\a", L"C:\\a" }, { L"C:\\a", L"C:\\ab" }, { L"C:\\A", L"c:\\a\\b" },
        { L"C:", L"C:\\a" }, { L"C:\\", L"C:\\a" }, { L"", L"C:\\a" }, { L"C:\\a", L"" },
        { L"\\\\srv\\s", L"\\\\srv\\s\\x" }, { L"\\a", L"\\a\\" },
    };
    for (i = 0; i < (int)(sizeof T / sizeof T[0]); ++i)
        sink += wia_pathisprefixw(T[i][0], T[i][1]);
    sink += wia_pathisprefixw(0, L"C:\\a");
    sink += wia_pathisprefixw(L"C:\\a", 0);
    sink += wia_pathisprefixw(0, 0);
    for (i = 0; i < 400; ++i) { a[i] = (i % 7 == 6) ? L'\\' : (wchar_t)(L'a' + i % 26);
                                b[i] = a[i]; }
    a[400] = 0; b[400] = 0;
    sink += wia_pathisprefixw(a, b);                       /* long, true */
    b[400] = L'x'; b[401] = 0;
    sink += wia_pathisprefixw(a, b);                       /* long, true, path longer */
    for (i = 0; i < 400; ++i) if (a[i] != L'\\') a[i] = (wchar_t)(a[i] - 32);
    sink += wia_pathisprefixw(a, b);                       /* long, case-differing: folds blocks */
    a[3] = L'#';
    sink += wia_pathisprefixw(a, b);                       /* long, false early */
}

#elif defined(T_251)
#define NAME "251-pathissamerootw"
extern int       wia_pathissamerootw(const wchar_t*, const wchar_t*);
extern wchar_t*  wia_pathskiprootw(const wchar_t*);
extern int       wia_pathcchskiproot_len(const wchar_t*);
extern void      wia_upcase_init(void);
#define SETUP() wia_upcase_init()
static void thunk(void){
    /* THREE entry points, and the seam between them is what this checks: the root parser is a LEAF
       that must touch no callee-saved register at all, wia_pathskiprootw keeps the path in rbx
       across a call to it, and wia_pathissamerootw keeps BOTH paths and then the root length across
       a call into change 167 -- which reaches for ymm0-ymm5.
       Driven: every branch of the root parser (drive, lone separator, plain UNC with an empty
       server and an empty share, the extended prefix in all three of its forms, and the volume GUID
       including the '[' case-fold trap), both NULL refusals, a relative path where the root skip
       fails, a shared root with a long tail, and a case-differing root, which is the only input
       that makes change 167 fold a block. */
    static wchar_t a[600], b[600];
    int i;
    static const wchar_t* R[] = {
        L"C:\\a\\b", L"C:", L"C:\\", L"c:x", L"1:\\", L"\\", L"\\a",
        L"\\\\", L"\\\\\\", L"\\\\s", L"\\\\s\\", L"\\\\s\\h",
        L"\\\\s\\h\\", L"\\\\s\\\\h", L"\\\\.\\C:\\",
        L"\\\\?\\C:", L"\\\\?\\C:\\", L"\\\\?\\a", L"\\\\?aa:",
        L"\\\\?\\UNC\\s\\h\\", L"\\\\?\\unc\\s\\h",
        L"\\\\?\\Volume{12345678-1234-1234-1234-123456789abc}\\",
        L"\\\\?\\Volume[12345678-1234-1234-1234-123456789abc}\\",
        L"rel", L"", L"a",
    };
    for (i = 0; i < (int)(sizeof R / sizeof R[0]); ++i) {
        sink += wia_pathcchskiproot_len(R[i]);
        sink += (wia_pathskiprootw(R[i]) != 0);
        sink += wia_pathissamerootw(R[i], R[i]);
    }
    sink += wia_pathcchskiproot_len(0);
    sink += (wia_pathskiprootw(0) != 0);
    sink += wia_pathissamerootw(0, L"C:\\a");
    sink += wia_pathissamerootw(L"C:\\a", 0);
    /* a shared root with a long tail: this is where change 167's walk does the work */
    a[0] = L'C'; a[1] = L':'; a[2] = L'\\';
    for (i = 0; i < 400; ++i) a[3 + i] = (i % 7 == 6) ? L'\\' : (wchar_t)(L'a' + i % 26);
    a[403] = 0;
    memcpy(b, a, sizeof(wchar_t) * 404);
    sink += wia_pathissamerootw(a, b);
    b[3] = L'#';
    sink += wia_pathissamerootw(a, b);
    for (i = 0; i < 400; ++i) if (b[3 + i] != L'\\') b[3 + i] = (wchar_t)(b[3 + i] - 32);
    sink += wia_pathissamerootw(a, b);          /* case-differing: folds blocks in 167 */
    b[0] = L'D';
    sink += wia_pathissamerootw(a, b);          /* a different root */
}

#elif defined(T_252)
#define NAME "252-rtlfindunicodesubstring"
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } ABI_USTR;
extern wchar_t* wia_findunicodesubstring(void*, void*, unsigned char);
extern int      wia_casemate_init(void);
#define SETUP() wia_casemate_init()
static void thunk(void){
    /* This one reaches for ymm0-ymm5 AND makes internal calls out of a PROC FRAME to two LEAF
       verifiers, so the stack-balance and DF bits matter as much as the register bits. The upper
       halves of ymm6-ymm15 are volatile and are not reported here, but the LOW halves are not, and
       a broadcast that picked xmm6 instead of xmm2 would show up as a violation.
       Driven: both modes; the empty-needle and needle-longer-than-haystack early exits, which
       return before any vector state exists; the sub-16 scalar-only path; the vector loop with a
       miss, with a hit inside a block, and with a hit in the scalar tail; the degenerate needle
       that makes the far-anchor choice fall back to m-1; and a non-ASCII haystack, which is the
       only input that exercises the case-partner broadcasts against real pairs. */
    static wchar_t h[600], n[64];
    ABI_USTR H, N;
    int i, k;
    for (i = 0; i < 600; ++i) h[i] = (wchar_t)(L'a' + (i % 5));
    H.Buffer = h; H.Length = 1200; H.MaximumLength = 1200;
    N.Buffer = n; N.MaximumLength = 128;

    N.Length = 0;                       sink += (wia_findunicodesubstring(&H, &N, 0) != 0);
    N.Length = 0;                       sink += (wia_findunicodesubstring(&H, &N, 1) != 0);
    H.Length = 0;                       sink += (wia_findunicodesubstring(&H, &N, 1) != 0);
    H.Length = 4;  N.Length = 20;       sink += (wia_findunicodesubstring(&H, &N, 0) != 0);

    /* sub-16 scalar-only: no vector state is ever created on this path */
    H.Length = 16; for (i = 0; i < 4; ++i) n[i] = L'z';
    N.Length = 8;  sink += (wia_findunicodesubstring(&H, &N, 0) != 0);
                   sink += (wia_findunicodesubstring(&H, &N, 1) != 0);

    /* the vector loop: miss, hit mid-block, hit in the scalar tail */
    H.Length = 1200;
    for (i = 0; i < 8; ++i) n[i] = L'z';
    N.Length = 16; sink += (wia_findunicodesubstring(&H, &N, 0) != 0);
                   sink += (wia_findunicodesubstring(&H, &N, 1) != 0);
    for (i = 0; i < 8; ++i) n[i] = h[300 + i];
    sink += (wia_findunicodesubstring(&H, &N, 0) != 0);
    for (i = 0; i < 8; ++i) n[i] = h[592 + i];
    sink += (wia_findunicodesubstring(&H, &N, 1) != 0);

    /* the degenerate needle: every character equal, so the far anchor falls back to m-1 */
    for (i = 0; i < 8; ++i) n[i] = L'a';
    sink += (wia_findunicodesubstring(&H, &N, 0) != 0);
    sink += (wia_findunicodesubstring(&H, &N, 1) != 0);

    /* non-ASCII, upper and lower, so the case-partner broadcasts face real pairs */
    for (k = 0; k < 2; ++k) {
        for (i = 0; i < 600; ++i) h[i] = (wchar_t)(0x0430 + (i % 20));
        for (i = 0; i < 8; ++i)   n[i] = (wchar_t)(0x0410 + ((i + (k ? 3 : 11)) % 20));
        sink += (wia_findunicodesubstring(&H, &N, 1) != 0);
        sink += (wia_findunicodesubstring(&H, &N, 0) != 0);
    }
}

#elif defined(T_254)
#define NAME "254-findstringordinal"
extern int wia_findstringordinal(unsigned long, const wchar_t*, int, const wchar_t*, int, int);
extern int wia_casemate_init(void);
#define SETUP() wia_casemate_init()
static void thunk(void){
    /* Eight non-volatile GPRs are saved here and rbp is used as a FRAME BASE for a 32-byte ymm
       spill, so a prologue/epilogue mismatch shows up as rbp not being restored rather than as a
       wrong answer. The function also makes internal calls out of a PROC FRAME into three leaves
       (two verifiers and the anchor/mask helpers), so stack balance matters as much as registers,
       and it writes the last error straight to gs:[0x68] -- which must not disturb anything else.
       Driven: all four modes; both early exits; the refusal paths (they return before any vector
       state exists); the sub-16 one-block path and the scalar tails; the forward block loop and the
       BACKWARD one with a hit in the last block, the first block and the head remainder; the
       degenerate needle that forces the anchor fallback; and non-ASCII, which is the only input
       that puts real pairs through the case-partner broadcasts. */
    static wchar_t h[600], n[64];
    int i, k;
    static const unsigned long M[4] = { 0x00400000, 0x00800000, 0x00100000, 0x00200000 };
    for (i = 0; i < 600; ++i) h[i] = (wchar_t)(L'a' + (i % 5));
    for (i = 0; i < 8; ++i)   n[i] = L'z';

    for (k = 0; k < 4; ++k) {
        sink += (unsigned)wia_findstringordinal(M[k], h, 600, n, 8, 0);   /* a full miss */
        sink += (unsigned)wia_findstringordinal(M[k], h, 600, n, 8, 1);
        sink += (unsigned)wia_findstringordinal(M[k], h, 600, n, 0, 0);   /* the empty needle */
        sink += (unsigned)wia_findstringordinal(M[k], h,   0, n, 8, 0);   /* the empty haystack */
        sink += (unsigned)wia_findstringordinal(M[k], h,   4, n,20, 0);   /* needle longer */
        sink += (unsigned)wia_findstringordinal(M[k], h,  12, n, 4, 0);   /* the scalar tail */
        sink += (unsigned)wia_findstringordinal(M[k], h,  20, n, 4, 0);   /* the ONE-BLOCK path */
        sink += (unsigned)wia_findstringordinal(M[k], h,  -1, n,-1, 0);   /* cch = -1 both */
    }
    /* hits: in the last block, the first block, and the head remainder a backward walk leaves */
    for (i = 0; i < 8; ++i) n[i] = h[560 + i];
    sink += (unsigned)wia_findstringordinal(0x00800000, h, 600, n, 8, 0);
    for (i = 0; i < 8; ++i) n[i] = h[3 + i];
    sink += (unsigned)wia_findstringordinal(0x00800000, h, 600, n, 8, 0);
    sink += (unsigned)wia_findstringordinal(0x00400000, h, 600, n, 8, 1);
    /* the degenerate needle: every character equal, so the far anchor falls back to m-1 */
    for (i = 0; i < 8; ++i) n[i] = L'a';
    sink += (unsigned)wia_findstringordinal(0x00400000, h, 600, n, 8, 0);
    sink += (unsigned)wia_findstringordinal(0x00800000, h, 600, n, 8, 1);
    /* the refusals */
    sink += (unsigned)wia_findstringordinal(0x00400000, 0,  -1, n, 8, 0);
    sink += (unsigned)wia_findstringordinal(0x00400000, h,  -1, 0, 8, 0);
    sink += (unsigned)wia_findstringordinal(0x00400000, h,  -2, n, 8, 0);
    sink += (unsigned)wia_findstringordinal(0x00C00000, h,  -1, n, 8, 0);
    sink += (unsigned)wia_findstringordinal(0x00400000, h,  -1, n, 8, 2);
    /* non-ASCII, both cases, so the case-partner broadcasts face real pairs */
    for (i = 0; i < 600; ++i) h[i] = (wchar_t)(0x0430 + (i % 20));
    for (i = 0; i < 8; ++i)   n[i] = (wchar_t)(0x0410 + ((i + 3) % 20));
    sink += (unsigned)wia_findstringordinal(0x00400000, h, 600, n, 8, 1);
    sink += (unsigned)wia_findstringordinal(0x00800000, h, 600, n, 8, 1);
}

#elif defined(T_255)
#define NAME "255-rtlfindlongestrunclear"
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } ABI_RBM;
extern unsigned long wia_findlongestrunclear(void*, unsigned long*);
static void thunk(void){
    /* Eight non-volatile GPRs are saved and rbp carries the loop index across an internal call to a
       leaf -- deliberately, because the obvious `push rax / push rcx` around that call would move
       rsp by eight bytes this function's unwind info does not describe. So a prologue/epilogue
       mismatch surfaces here as rbp or r15 not being restored rather than as a wrong answer. The
       function also uses ymm0-ymm3 for the four-word skip and must VZEROUPPER on every exit.
       Driven: the all-ones and all-zero vector skips, a mixed bitmap that takes neither, the
       degenerate sizes (0 bits, 1 bit, a bitmap with no clear bits), the ODD trailing ULONG that
       must be read as 32 bits, the slack masking, and a long run spanning many words. */
    static unsigned long b[128];
    ABI_RBM bm;
    unsigned long ix = 0;
    int i, k;

    bm.Buffer = b;
    for (k = 0; k < 4; ++k) {
        for (i = 0; i < 128; ++i)
            b[i] = (k == 0) ? 0xFFFFFFFFul                      /* the all-ones skip */
                 : (k == 1) ? 0ul                               /* the all-zero skip */
                 : (k == 2) ? 0xA5A5A5A5ul                      /* mixed: neither skip */
                            : ((i & 1) ? 0xFFFFFFFFul : 0ul);   /* half and half */
        for (i = 0; i < 20; ++i) {
            bm.SizeOfBitMap = (unsigned long)(1 + i * 199);      /* odd and even ULONG counts */
            sink += wia_findlongestrunclear(&bm, &ix);
            sink += ix;
        }
        bm.SizeOfBitMap = 4096;
        sink += wia_findlongestrunclear(&bm, &ix);
        sink += ix;
    }
    /* the degenerate sizes, and a long run spanning many words */
    for (i = 0; i < 128; ++i) b[i] = 0xFFFFFFFFul;
    bm.SizeOfBitMap = 0;    sink += wia_findlongestrunclear(&bm, &ix);
    bm.SizeOfBitMap = 1;    sink += wia_findlongestrunclear(&bm, &ix);
    bm.SizeOfBitMap = 33;   sink += wia_findlongestrunclear(&bm, &ix);
    bm.SizeOfBitMap = 4096; sink += wia_findlongestrunclear(&bm, &ix);
    for (i = 40; i < 3000; ++i) b[i >> 5] &= ~(1ul << (i & 31));
    bm.SizeOfBitMap = 4096; sink += wia_findlongestrunclear(&bm, &ix);
    sink += ix;
    sink += wia_findlongestrunclear(0, &ix);
}

#elif defined(T_257)
#define NAME "257-rtlnumberofsetbits"
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } ABI_RBM;
extern unsigned long wia_numberofsetbits(void*);
extern unsigned long wia_numberofclearbits(void*);
extern unsigned long wia_numberofsetbitsinrange(void*, unsigned long, unsigned long);
extern unsigned long wia_numberofclearbitsinrange(void*, unsigned long, unsigned long);
static void thunk(void){
    /* THIS CHANGE IS WHY THE GATE EXISTS. Its four entry stubs pass a selector to a shared framed
       body, and the first version put it in r13 -- a NON-VOLATILE register -- BEFORE that body's
       prologue saved it, destroying the caller's r13. Correctness passed at /Od, where the compiler
       spills everything, and the /O2 build died with an access violation before its first line of
       output. The selector now lives in r10, and this driver is what proves it stays that way.
       Driven: all four exports; the single-word LEAF fast path and the framed body; the vector
       loop, its scalar remainder and both masked partial words; the refusal paths, which return
       before any ymm register is touched; and a bitmap large enough that the VPSHUFB body runs
       many times, since it is the only path that writes ymm0-ymm5 and must VZEROUPPER. */
    static unsigned long b[512];
    ABI_RBM bm;
    int i, k;
    for (i = 0; i < 512; ++i) b[i] = 0xA5A5A5A5ul ^ (unsigned long)(i * 0x01010101ul);
    bm.Buffer = b;

    for (k = 0; k < 6; ++k) {
        static const unsigned long SZ[6] = { 1, 33, 64, 256, 4096, 16384 };
        bm.SizeOfBitMap = SZ[k];
        sink += wia_numberofsetbits(&bm);
        sink += wia_numberofclearbits(&bm);
        sink += wia_numberofsetbitsinrange(&bm, 0, SZ[k]);
        sink += wia_numberofclearbitsinrange(&bm, 0, SZ[k]);
        if (SZ[k] > 8) {
            sink += wia_numberofsetbitsinrange(&bm, 3, SZ[k] - 5);     /* unaligned both ends */
            sink += wia_numberofclearbitsinrange(&bm, 5, 3);           /* inside one word */
            sink += wia_numberofsetbitsinrange(&bm, 1, 1);
        }
        sink += wia_numberofsetbitsinrange(&bm, 0, 0);                 /* refused */
        sink += wia_numberofsetbitsinrange(&bm, SZ[k], 1);             /* refused */
        sink += wia_numberofclearbitsinrange(&bm, 0, SZ[k] + 1);       /* refused */
    }
    bm.SizeOfBitMap = 0;
    sink += wia_numberofsetbits(&bm);
    sink += wia_numberofclearbits(&bm);
    sink += wia_numberofsetbitsinrange(&bm, 0, 1);
    sink += wia_numberofsetbits(0);
}

#elif defined(T_259)
#define NAME "259-rtlarebitsset"
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } ABI_RBM;
extern unsigned char wia_arebitsset(void*, unsigned long, unsigned long);
extern unsigned char wia_arebitsclear(void*, unsigned long, unsigned long);
static void thunk(void){
    /* A LEAF -- no prologue, no saved registers, no unwind data -- which is the point: it must
       therefore not touch a non-volatile register at all, and the only way to be sure is to drive
       every path and look. Armed PER CALL (CALL4), not around the thunk: a thunk that uses r15 for
       its own loop hides an implementation that destroys r15, as change 258 demonstrated.
       Driven: both exports; the one-word and two-word masked compares; the general path with its
       vector loop, its scalar remainder and both masked ends; the early exit on the first word,
       which returns without ever touching a ymm register and therefore without VZEROUPPER; and
       every refusal. */
    static unsigned long b[512];
    ABI_RBM bm;
    int i, k;
    bm.Buffer = b;
    for (k = 0; k < 3; ++k) {
        for (i = 0; i < 512; ++i)
            b[i] = (k == 0) ? 0xFFFFFFFFul : (k == 1) ? 0ul : 0xA5A5A5A5ul;
        bm.SizeOfBitMap = 16384;
        for (i = 0; i < 40; ++i) {
            unsigned long s = (unsigned long)(i * 397);      /* on and off every boundary */
            CALL4(wia_arebitsset,   &bm, s, 1, 0);           /* one word */
            CALL4(wia_arebitsclear, &bm, s, 1, 0);
            CALL4(wia_arebitsset,   &bm, s, 40, 0);          /* two words */
            CALL4(wia_arebitsclear, &bm, s, 40, 0);
            CALL4(wia_arebitsset,   &bm, s, 500, 0);         /* the general path, no vector step */
            CALL4(wia_arebitsset,   &bm, s, 8000, 0);        /* ... and with many of them */
            CALL4(wia_arebitsclear, &bm, s, 8000, 0);
        }
        bm.SizeOfBitMap = 33;                                /* an odd trailing ULONG */
        CALL4(wia_arebitsset,   &bm, 0, 33, 0);
        CALL4(wia_arebitsclear, &bm, 1, 32, 0);
    }
    /* the refusals: every one returns before a vector register exists */
    bm.SizeOfBitMap = 100;
    CALL4(wia_arebitsset,   &bm, 0, 0, 0);                   /* length zero */
    CALL4(wia_arebitsclear, &bm, 0, 0, 0);
    CALL4(wia_arebitsset,   &bm, 100, 1, 0);                 /* a start at the end */
    CALL4(wia_arebitsset,   &bm, 0, 101, 0);                 /* one bit too long */
    CALL4(wia_arebitsset,   &bm, 0, 0xFFFFFFFFul, 0);        /* an absurd length */
}

#elif defined(T_256)
#define NAME "256-rtlfindsetbits"
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } ABI_RBM;
extern unsigned long wia_findsetbits(void*, unsigned long, unsigned long);
extern unsigned long wia_findclearbits(void*, unsigned long, unsigned long);
static void thunk(void){
    /* Two entry stubs tail-jump into one framed body, which then calls a leaf that owns ALL EIGHT
       non-volatile registers -- and the vector phase keeps its cursor and its limit across that
       call, so a prologue that saved the wrong set would show up here rather than as a wrong
       answer. Armed PER CALL (CALL4), not around the thunk: a thunk that uses r15 for its own loop
       hides an implementation that destroys r15, which is exactly what change 258 demonstrated.
       Driven: both exports; every witness block size, since N picks it -- pairs (3..6), nibbles
       (7..14), bytes (15..30), words (31..62), dwords (63..126) and qwords (127+); the first-word
       fast answer; the inline rebuild and the general-scanner fallback near both edges; the wrap,
       which runs the whole scan twice; and the refusals, which return before a ymm is touched. */
    static unsigned long b[512];
    static const unsigned long NS[10] = { 1, 2, 3, 8, 16, 40, 64, 200, 1000, 5000 };
    ABI_RBM bm;
    int i, k, n;
    bm.Buffer = b;
    for (k = 0; k < 5; ++k) {
        for (i = 0; i < 512; ++i)
            b[i] = (k == 0) ? 0xFFFFFFFFul
                 : (k == 1) ? 0ul
                 : (k == 2) ? 0xA5A5A5A5ul
                 : (k == 3) ? ((i & 1) ? 0xFFFFFFFFul : 0ul)
                            : ((i & 7) ? 0ul : 0xFFFFFFFFul);
        for (n = 0; n < 10; ++n) {
            bm.SizeOfBitMap = 16384;
            CALL4(wia_findsetbits,   &bm, NS[n], 0, 0);
            CALL4(wia_findclearbits, &bm, NS[n], 0, 0);
            CALL4(wia_findsetbits,   &bm, NS[n], 16000, 0);   /* forces the wrap: two full scans */
            CALL4(wia_findclearbits, &bm, NS[n], 16000, 0);
            CALL4(wia_findsetbits,   &bm, NS[n], 37, 0);      /* a hint off every boundary */
            bm.SizeOfBitMap = 331;                            /* smaller than one chunk */
            CALL4(wia_findsetbits,   &bm, NS[n], 0, 0);
            CALL4(wia_findclearbits, &bm, NS[n], 5, 0);
            bm.SizeOfBitMap = 33;                             /* an odd trailing ULONG */
            CALL4(wia_findsetbits,   &bm, NS[n], 0, 0);
        }
    }
    /* the refusals and the degenerate arguments */
    bm.SizeOfBitMap = 0;    CALL4(wia_findsetbits,   &bm, 4, 0, 0);
    bm.SizeOfBitMap = 100;  CALL4(wia_findsetbits,   &bm, 0, 77, 0);   /* N = 0 has its own rule */
                            CALL4(wia_findclearbits, &bm, 0, 77, 0);
                            CALL4(wia_findsetbits,   &bm, 101, 0, 0);  /* more bits than exist */
    CALL4(wia_findsetbits, 0, 4, 0, 0);
}

#elif defined(T_258)
#define NAME "258-rtlfindclearruns"
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } ABI_RBM;
typedef struct { unsigned long StartingIndex; unsigned long NumberOfBits; } ABI_RUN;
extern unsigned long wia_findclearruns(void*, ABI_RUN*, unsigned long, unsigned char);
static void thunk(void){
    /* ALL EIGHT non-volatile GPRs hold loop state here, and the two scans SHARE them: r12 is the
       SortByLength flag on the way in, then the enumeration mask on the sorted path and the byte
       table's base on the unsorted one. A prologue that saved the wrong set, or a path that
       returned without the matching epilogue, would show up here as one of those eight not being
       restored rather than as a wrong answer -- and both scans have their own exit.
       Driven: both forms; the 64-bit and 32-bit uniform skips and the byte walk between them; the
       masked final byte and the 32-bit tail read; a capacity smaller, equal to and larger than the
       number of runs, so the insertion both shifts and falls off the end; the early return when an
       unsorted array fills mid-byte; and the degenerate arguments, which return before the loop. */
    static unsigned long b[128];
    static ABI_RUN out[80];
    ABI_RBM bm;
    int i, k, c;
    static const unsigned long CAP[4] = { 1, 4, 17, 70 };

    bm.Buffer = b;
    for (k = 0; k < 5; ++k) {
        for (i = 0; i < 128; ++i)
            b[i] = (k == 0) ? 0xFFFFFFFFul                      /* no clear bits at all */
                 : (k == 1) ? 0ul                               /* one enormous run */
                 : (k == 2) ? 0xA5A5A5A5ul                      /* a run every two or three bits */
                 : (k == 3) ? ((i & 1) ? 0xFFFFFFFFul : 0ul)    /* uniform over a ULONG, not a pair */
                            : ((i & 3) ? 0xFFFFFFFFul : 0x0F0F0F0Ful);
        for (c = 0; c < 4; ++c) {
            for (i = 0; i < 12; ++i) {
                bm.SizeOfBitMap = (unsigned long)(1 + i * 331);  /* on and off the byte boundary */
                CALL4(wia_findclearruns, &bm, out, CAP[c], 1);
                CALL4(wia_findclearruns, &bm, out, CAP[c], 0);
            }
            bm.SizeOfBitMap = 4096;
            CALL4(wia_findclearruns, &bm, out, CAP[c], 1);
            CALL4(wia_findclearruns, &bm, out, CAP[c], 0);
        }
    }
    /* the degenerate arguments: every one of them returns before the loop is entered */
    bm.SizeOfBitMap = 0;    CALL4(wia_findclearruns, &bm, out, 4, 1);
    bm.SizeOfBitMap = 4096; CALL4(wia_findclearruns, &bm, out, 0, 1);
    CALL4(wia_findclearruns, &bm, 0, 4, 1);
    CALL4(wia_findclearruns, 0, out, 4, 1);
}

#elif defined(T_261)
#define NAME "261-rtlfindnextforwardrunclear"
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } ABI_RBM;
extern unsigned long wia_findnextforwardrunclear(void*, unsigned long, unsigned long*);
extern unsigned long wia_findlastbackwardrunclear(void*, unsigned long, unsigned long*);
static void thunk(void){
    /* TWO LEAVES -- no prologue, no saved registers, no unwind data -- which is exactly why this
       has to be driven rather than reasoned about: both functions keep everything in the seven
       volatile registers and the shadow space the caller already reserved, and a single stray
       push or a stray r12 would be invisible until something else broke. Armed PER CALL (CALL4),
       not around the thunk: a thunk that uses r15 for its own loop hides an implementation that
       destroys r15, as change 258 demonstrated.

       Every path of both scans is driven: the answer inside the first word; the two scalar
       pre-steps; the vector skip and its mask-derived hit; the scalar walk after the vector loop
       falls out; the last word with its slack; the run that reaches the end of the bitmap; the
       run that reaches bit zero going backward; nothing-found in both directions; and both
       refusals at FromIndex at or past SizeOfBitMap. */
    static unsigned long b[512];
    unsigned long start = 0;
    ABI_RBM bm;
    int i, k;
    bm.Buffer = b;
    for (k = 0; k < 4; ++k) {
        for (i = 0; i < 512; ++i)
            b[i] = (k == 0) ? 0xFFFFFFFFul : (k == 1) ? 0ul
                 : (k == 2) ? 0xA5A5A5A5ul : ((i & 7) ? 0xFFFFFFFFul : 0xFFFF0000ul);
        bm.SizeOfBitMap = 16384;
        for (i = 0; i < 40; ++i) {
            unsigned long f = (unsigned long)(i * 397);      /* on and off every boundary */
            CALL4(wia_findnextforwardrunclear,  &bm, f, &start, 0);
            CALL4(wia_findlastbackwardrunclear, &bm, f, &start, 0);
        }
        CALL4(wia_findnextforwardrunclear,  &bm, 0, &start, 0);        /* the longest scans */
        CALL4(wia_findlastbackwardrunclear, &bm, 16383, &start, 0);
        bm.SizeOfBitMap = 33;                                          /* an odd trailing ULONG */
        CALL4(wia_findnextforwardrunclear,  &bm, 1, &start, 0);
        CALL4(wia_findlastbackwardrunclear, &bm, 32, &start, 0);
        bm.SizeOfBitMap = 64;                                          /* ... and an even one */
        CALL4(wia_findnextforwardrunclear,  &bm, 1, &start, 0);
        CALL4(wia_findlastbackwardrunclear, &bm, 63, &start, 0);
        bm.SizeOfBitMap = 200;                                         /* too short to vector at all */
        CALL4(wia_findnextforwardrunclear,  &bm, 3, &start, 0);
        CALL4(wia_findlastbackwardrunclear, &bm, 199, &start, 0);
    }
    /* the refusals: both return before anything at all is loaded */
    bm.SizeOfBitMap = 100;
    CALL4(wia_findnextforwardrunclear,  &bm, 100, &start, 0);
    CALL4(wia_findlastbackwardrunclear, &bm, 100, &start, 0);
    CALL4(wia_findnextforwardrunclear,  &bm, 0xFFFFFFFFul, &start, 0);
    CALL4(wia_findlastbackwardrunclear, &bm, 0xFFFFFFFFul, &start, 0);
    bm.SizeOfBitMap = 0;
    CALL4(wia_findnextforwardrunclear,  &bm, 0, &start, 0);
    CALL4(wia_findlastbackwardrunclear, &bm, 0, &start, 0);
}

#elif defined(T_262)
#define NAME "262-rtlfindsetbitsandclear"
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } ABI_RBM;
extern unsigned long wia_findsetbitsandclear(void*, unsigned long, unsigned long);
extern unsigned long wia_findclearbitsandset(void*, unsigned long, unsigned long);
static void thunk(void){
    /* THIS ONE IS NOT A LEAF, and it is the only thing in the bitmap family here that is not: it
       CALLS change 256's search and then mutates, so it has a real frame with real unwind data and
       it must both preserve the register contract itself AND not be broken by the callee. It keeps
       the bitmap and the count in the frame it has to allocate anyway rather than in rbx and rsi,
       so a violation here would mean either the frame arithmetic or change 256 underneath.

       Armed PER CALL (CALL4), not around the thunk: a thunk that uses r15 for its own loop hides
       an implementation that destroys r15, as change 258 demonstrated.

       Every path of both halves is driven: the 64-bit fast path that never makes the call (both
       its found and not-found arms, at one and two ULONGs), the general path through change 256,
       the mutation inside one word, across two words, and across enough words to reach the vector
       fill and its overlapping tail; not-found and NumberToFind = 0, which write nothing at all;
       and every refusal. THE BUFFER IS REBUILT BETWEEN CALLS because these calls CONSUME what they
       find -- a thunk that did not would be driving the not-found path over and over while
       believing it was driving the mutation. */
    static unsigned long b[512];
    ABI_RBM bm;
    int i, k, t;
    static const unsigned long NS[8] = { 1, 2, 8, 33, 64, 200, 1024, 9000 };
    bm.Buffer = b;
    for (k = 0; k < 4; ++k) {
        for (t = 0; t < 8; ++t) {
            for (i = 0; i < 512; ++i)
                b[i] = (k == 0) ? 0xFFFFFFFFul : (k == 1) ? 0ul
                     : (k == 2) ? 0xA5A5A5A5ul : ((i & 3) ? 0xFFFFFFFFul : 0ul);
            bm.SizeOfBitMap = 16384;
            CALL4(wia_findsetbitsandclear, &bm, NS[t], 0, 0);
            CALL4(wia_findclearbitsandset, &bm, NS[t], 0, 0);
            CALL4(wia_findsetbitsandclear, &bm, NS[t], 9000, 0);   /* forces the wrap */
            CALL4(wia_findclearbitsandset, &bm, NS[t], 9000, 0);
            CALL4(wia_findsetbitsandclear, &bm, NS[t], 37, 0);     /* a hint off every boundary */

            /* the 64-bit fast path: one ULONG and two, found and not found */
            for (i = 0; i < 512; ++i)
                b[i] = (k == 0) ? 0xFFFFFFFFul : (k == 1) ? 0ul
                     : (k == 2) ? 0xA5A5A5A5ul : 0xFFFF0000ul;
            bm.SizeOfBitMap = 32;
            CALL4(wia_findsetbitsandclear, &bm, NS[t], 0, 0);
            CALL4(wia_findclearbitsandset, &bm, NS[t], 5, 0);
            bm.SizeOfBitMap = 33;
            CALL4(wia_findsetbitsandclear, &bm, NS[t], 1, 0);
            CALL4(wia_findclearbitsandset, &bm, NS[t], 40, 0);
            bm.SizeOfBitMap = 64;
            CALL4(wia_findsetbitsandclear, &bm, NS[t], 0, 0);
            CALL4(wia_findclearbitsandset, &bm, NS[t], 63, 0);
            bm.SizeOfBitMap = 7;
            CALL4(wia_findsetbitsandclear, &bm, NS[t], 0, 0);
        }
    }
    /* the refusals and the arms that write nothing */
    bm.SizeOfBitMap = 100;
    CALL4(wia_findsetbitsandclear, &bm, 0, 0, 0);            /* N = 0, an index and no write */
    CALL4(wia_findclearbitsandset, &bm, 0, 77, 0);
    CALL4(wia_findsetbitsandclear, &bm, 0, 9999, 0);         /* ... with the hint past the end */
    CALL4(wia_findsetbitsandclear, &bm, 101, 0, 0);          /* more bits than exist */
    CALL4(wia_findclearbitsandset, &bm, 101, 0, 0);
    bm.SizeOfBitMap = 0;
    CALL4(wia_findsetbitsandclear, &bm, 4, 0, 0);            /* a bitmap of no bits */
    CALL4(wia_findsetbitsandclear, &bm, 0, 0, 0);
}

#elif defined(T_263)
#define NAME "263-rtlcompareunicodestrings"
extern long wia_compareunicodestrings(const wchar_t*, size_t, const wchar_t*, size_t, unsigned char);
extern unsigned short wia_upcase[65536];
extern void wia_upcase_init(void);
#define SETUP() wia_upcase_init()
static void thunk(void){
    /* THIS GATE ALREADY EARNED ITS KEEP ON THIS CHANGE. The first draft parked four constants in
       ymm4..ymm7, and the low 128 bits of xmm6-xmm15 are NON-VOLATILE under Win64 -- so it
       destroyed two registers belonging to the caller. The symptom was not a crash: the benchmark
       printed 0.00 ns for every case-insensitive row, because the compiler had a double live in
       xmm6 across the call and the number being formatted had been overwritten. A gate that only
       checked GPRs would have passed it.

       Driven: the vector loop and the scalar tail on both flags; a block that disagrees only in
       case, which is the in-vector fold; a block containing a character at or above 0x80, which is
       the table fallback; the length tie-break; zero lengths; and lengths either side of the
       sixteen-character block. */
    static wchar_t a[512], b[512];
    int i, k, t;
    static const size_t L[8] = { 0, 1, 7, 15, 16, 17, 33, 400 };
    long sink = 0;
    for (k = 0; k < 5; ++k) {
        for (i = 0; i < 512; ++i) {
            wchar_t c = (wchar_t)(0x61 + (i % 26));
            a[i] = c;
            b[i] = (k == 0) ? c                                   /* identical */
                 : (k == 1) ? (wchar_t)(c - 32)                   /* differs only in case */
                 : (k == 2) ? (wchar_t)(0x00E0 + (i % 24))        /* forces the table */
                 : (k == 3) ? (wchar_t)(c + 1)                    /* differs everywhere */
                            : (wchar_t)0xFFFF;                    /* the top of the range */
            if (k == 2) a[i] = (wchar_t)(0x00C0 + (i % 24));
        }
        for (t = 0; t < 8; ++t) {
            sink += CALL5(wia_compareunicodestrings, a, L[t], b, L[t], 0);
            sink += CALL5(wia_compareunicodestrings, a, L[t], b, L[t], 1);
            sink += CALL5(wia_compareunicodestrings, a, L[t], b, 400, 0);   /* the lengths decide */
            sink += CALL5(wia_compareunicodestrings, a, 400, b, L[t], 1);
        }
    }
    sink += CALL5(wia_compareunicodestrings, 0, 0, 0, 0, 0);       /* NULL, never read at length 0 */
    sink += CALL5(wia_compareunicodestrings, 0, 0, b, 3, 1);
    if (sink == 0x7FFFFFFF) printf("");
}

#elif defined(T_264)
#define NAME "264-rtlinitutf8string"
typedef struct { unsigned short Length, MaximumLength; char* Buffer; } ABI_U8STR;
extern void wia_rtlinitutf8string(ABI_U8STR*, const char*);
static void thunk(void){
    /* The implementation here is change 095's, reached through a LINKER ALIAS -- so what this gate
       is really checking is that 095's code is still ABI-clean when it is entered under this
       export's name, and that the alias itself introduces nothing. It is cheap to run and the
       alternative is assuming it.

       Armed PER CALL (CALL4), not around the thunk: a thunk that uses r15 for its own loop hides an
       implementation that destroys r15, as change 258 demonstrated. Driven: the empty string, every
       length through the vector loop and its tail, a string long enough to saturate both USHORT
       fields, a string ending right at a page so the page-safe entry is exercised, and NULL. */
    static char buf[70001];
    ABI_U8STR d;
    static const int LENS[10] = { 0, 1, 7, 31, 32, 33, 63, 64, 300, 70000 };
    int i, k;
    for (i = 0; i < 10; ++i) {
        for (k = 0; k < LENS[i]; ++k) buf[k] = (char)(0x41 + (k % 26));
        buf[LENS[i]] = 0;
        CALL4(wia_rtlinitutf8string, &d, buf, 0, 0);
        CALL4(wia_rtlinitutf8string, &d, buf + (LENS[i] ? 1 : 0), 0, 0);   /* an odd alignment */
    }
    CALL4(wia_rtlinitutf8string, &d, 0, 0, 0);                             /* the NULL source */
}

#elif defined(T_265)
#define NAME "265-rtlappendasciiztostring"
typedef struct { unsigned short Length, MaximumLength; char* Buffer; } ABI_ASTR;
extern long wia_appendasciiztostring(ABI_ASTR*, const char*);
static void thunk(void){
    /* A LEAF -- no prologue, no saved registers, no unwind data -- which is the point: it keeps the
       source pointer in the caller's shadow space rather than in a non-volatile register, because
       the inlined scan uses edx as a scratch and the pointer does not survive it. If that parking
       were done with a push instead, this gate is what would notice.

       Armed PER CALL (CALL4), not around the thunk: a thunk that uses r15 for its own loop hides an
       implementation that destroys r15, as change 258 demonstrated.

       Driven: the empty source and NULL, every rung of the small-copy ladder (1, 2..3, 4..7, 8..15,
       16..31 bytes), the 32-byte vector loop and its overlapping tail, a source big enough to run
       the scan through its 64-byte block loop, and the refusal path at several sizes -- which must
       return without writing anything at all. */
    static char src[9000];
    static char dst[9100];
    ABI_ASTR d;
    static const int LENS[14] = { 0, 1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 8000 };
    int i, k;
    for (k = 0; k < 9000; ++k) src[k] = (char)('a' + (k % 26));
    for (i = 0; i < 14; ++i) {
        src[LENS[i]] = 0;
        d.Length = 0; d.MaximumLength = 9000; d.Buffer = dst;
        CALL4(wia_appendasciiztostring, &d, src, 0, 0);          /* it fits */
        d.Length = 0; d.MaximumLength = 9000; d.Buffer = dst;
        CALL4(wia_appendasciiztostring, &d, src + 1, 0, 0);      /* ... at an odd alignment */
        d.Length = 0;
        d.MaximumLength = (unsigned short)(LENS[i] ? LENS[i] - 1 : 0);
        CALL4(wia_appendasciiztostring, &d, src, 0, 0);          /* ... and it does not */
        src[LENS[i]] = (char)('a' + (LENS[i] % 26));
    }
    d.Length = 0; d.MaximumLength = 9000; d.Buffer = dst;
    CALL4(wia_appendasciiztostring, &d, 0, 0, 0);                /* the NULL source */
}

#elif defined(T_266)
#define NAME "266-rtliszeromemory"
extern unsigned char wia_iszeromemory(const void*, size_t);
static void thunk(void){
    /* A LEAF -- no prologue, no saved registers, no unwind data -- so it must not touch a
       non-volatile register at all. Armed PER CALL (CALL4), not around the thunk: a thunk that
       uses r15 for its own loop hides an implementation that destroys r15, as change 258
       demonstrated.

       Driven: every rung of the sub-32-byte ladder (1, 2..3, 4..7, 8..15, 16..31), the 32-byte
       loop, the 128-byte block loop, the overlapping final vector, the early exit at the very
       first vector, and the zero length that never reads the pointer at all. Both answers are
       produced at every size, because the TRUE path and the false path leave the function through
       different exits and only one of them runs VZEROUPPER at the same place. */
    static unsigned char buf[4096];
    static const size_t LENS[16] = { 0, 1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 127, 128, 4000 };
    int i, k;
    unsigned long long sink = 0;
    for (i = 0; i < 16; ++i) {
        for (k = 0; k < 4096; ++k) buf[k] = 0;
        sink += CALL4(wia_iszeromemory, buf, LENS[i], 0, 0);          /* all zero: TRUE */
        if (LENS[i]) {
            buf[LENS[i] - 1] = 0x01;                                   /* the last byte */
            sink += CALL4(wia_iszeromemory, buf, LENS[i], 0, 0);
            buf[LENS[i] - 1] = 0;
            buf[0] = 0x80;                                             /* the first byte */
            sink += CALL4(wia_iszeromemory, buf, LENS[i], 0, 0);
            buf[0] = 0;
            buf[LENS[i] / 2] = 0x40;                                   /* the middle */
            sink += CALL4(wia_iszeromemory, buf, LENS[i], 0, 0);
            buf[LENS[i] / 2] = 0;
        }
        sink += CALL4(wia_iszeromemory, buf + 1, LENS[i], 0, 0);       /* an odd alignment */
    }
    CALL4(wia_iszeromemory, 0, 0, 0, 0);                               /* NULL at length zero */
    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_267)
#define NAME "267-rtlcrc32"
extern unsigned long wia_crc32(const void*, size_t, unsigned long);
extern int wia_crc32_tables_init(void);
#define SETUP() wia_crc32_tables_init()
static void thunk(void){
    /* TWO SHAPES IN ONE FUNCTION and the gate has to reach both: a LEAF with no prologue answers
       anything under 192 bytes, and a PROC FRAME body with four pushed registers -- rbx, rsi, rdi
       and r12 -- handles everything larger. The framed half is the one that can get the unwind
       data wrong, and the leaf half is the one that must not touch a non-volatile register at all.

       Armed PER CALL (CALL4), not around the thunk: a thunk that uses r15 for its own loop hides
       an implementation that destroys r15, as change 258 demonstrated.

       Driven: lengths either side of BOTH block boundaries (192 and 3072), every tail residue
       from 1 to 7, the exact block sizes, a non-zero initial CRC, and the zero length. */
    static unsigned char buf[8192];
    static const size_t LENS[18] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 63, 64, 191, 192, 193,
                                     1024, 3071, 3072, 6145 };
    int i, k;
    unsigned long sink = 0;
    for (k = 0; k < 8192; ++k) buf[k] = (unsigned char)(k * 37 + 5);
    for (i = 0; i < 18; ++i) {
        sink += CALL4(wia_crc32, buf, LENS[i], 0, 0);
        sink += CALL4(wia_crc32, buf, LENS[i], 0xDEADBEEFul, 0);       /* a non-zero running CRC */
        sink += CALL4(wia_crc32, buf + 1, LENS[i], 0, 0);              /* an odd alignment */
    }
    if (sink == 0x7FFFFFFFul) printf("");
}

#elif defined(T_268)
#define NAME "268-rtlunicodestringtoutf8string"
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } WIA_USTR;
typedef struct { unsigned short Length, MaximumLength; char* Buffer; } WIA_U8STR;
extern long wia_unicodestringtoutf8string(WIA_U8STR*, const WIA_USTR*, unsigned char);
extern long wia_utf8stringtounicodestring(WIA_USTR*, const WIA_U8STR*, unsigned char);
static void thunk(void){
    /* TWO FRAMED FUNCTIONS AND FIVE PATHS EACH, and the gate has to reach all of them: the
       one-pass conversion into the caller buffer, the shortfall that leaves it partly filled,
       the sizing pass a tight destination forces, the USHORT-field refusal, and the allocating
       path -- which is the only one that calls out to the heap, so it is the only one whose
       register damage could come from somewhere other than this repository.

       Both functions are PROC FRAME with an allocated frame and nothing pushed, which is the shape
       whose unwind data can be wrong without any test noticing. Armed PER CALL (CALL4), because a
       thunk that uses a register for its own loop hides an implementation that destroys it.

       THE ALLOCATING CALLS FREE WHAT THEY TAKE, through the paired exports. Without that this
       thunk leaks a block per call and the run ends up measuring the heap. */
    static wchar_t wbig[32767];
    static char    u8big[40000];
    static char    obuf[70000];
    static wchar_t owbuf[40000];
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    void (WINAPI *freeu8)(WIA_U8STR*) = (void (WINAPI*)(WIA_U8STR*))GetProcAddress(h, "RtlFreeUTF8String");
    void (WINAPI *freeu )(WIA_USTR*)  = (void (WINAPI*)(WIA_USTR*)) GetProcAddress(h, "RtlFreeUnicodeString");
    static const int LENS[9] = { 0, 1, 7, 8, 9, 40, 300, 4000, 21845 };
    WIA_USTR uin, uout;
    WIA_U8STR ain, aout;
    unsigned long long sink = 0;
    int i, k;

    for (k = 0; k < 32767; ++k)
        wbig[k] = (wchar_t)((k % 5 == 0) ? (0x20AC + (k & 15))
                          : (k % 5 == 1) ? (0x00E9 + (k & 15))
                          : (k % 5 == 2) ? 0xD83D
                          : (k % 5 == 3) ? (0xDE00 + (k & 15))
                                         : (wchar_t)(0x61 + (k & 15)));
    for (k = 0; k < 40000; ++k)
        u8big[k] = (char)((k % 3 == 0) ? (0x61 + (k & 15)) : (k % 3 == 1) ? 0xC3 : 0xA9);

    for (i = 0; i < 9; ++i) {
        int n = LENS[i];
        uin.Buffer = wbig; uin.Length = (unsigned short)(n * 2); uin.MaximumLength = uin.Length;
        ain.Buffer = u8big; ain.Length = (unsigned short)(n < 32767 ? n : 32767);
        ain.MaximumLength = ain.Length;

        /* a generous destination: the one-pass path in both directions */
        aout.Buffer = obuf;  aout.Length = 0; aout.MaximumLength = 0xFFFF;
        sink += CALL4(wia_unicodestringtoutf8string, &aout, &uin, 0, 0);
        uout.Buffer = owbuf; uout.Length = 0; uout.MaximumLength = 0xFFFF;
        sink += CALL4(wia_utf8stringtounicodestring, &uout, &ain, 0, 0);

        /* a tight destination: the sizing pass, and the shortfall that partly fills a buffer */
        aout.MaximumLength = (unsigned short)(n + 1);
        sink += CALL4(wia_unicodestringtoutf8string, &aout, &uin, 0, 0);
        uout.MaximumLength = (unsigned short)(n + 2);
        sink += CALL4(wia_utf8stringtounicodestring, &uout, &ain, 0, 0);

        /* capacity zero, which is its own status in one direction and not the other */
        aout.MaximumLength = 0;
        sink += CALL4(wia_unicodestringtoutf8string, &aout, &uin, 0, 0);
        uout.MaximumLength = 0;
        sink += CALL4(wia_utf8stringtounicodestring, &uout, &ain, 0, 0);

        /* and the allocating path, freed by the paired export */
        memset(&aout, 0, sizeof aout);
        sink += CALL4(wia_unicodestringtoutf8string, &aout, &uin, 1, 0);
        if (freeu8) freeu8(&aout);
        memset(&uout, 0, sizeof uout);
        sink += CALL4(wia_utf8stringtounicodestring, &uout, &ain, 1, 0);
        if (freeu) freeu(&uout);
    }

    /* the USHORT-field refusal: 32767 three-byte characters cannot fit a USHORT Length */
    for (k = 0; k < 32767; ++k) wbig[k] = (wchar_t)(0x20AC + (k & 15));
    uin.Buffer = wbig; uin.Length = 65534; uin.MaximumLength = 65534;
    aout.Buffer = obuf; aout.Length = 0; aout.MaximumLength = 0xFFFF;
    sink += CALL4(wia_unicodestringtoutf8string, &aout, &uin, 0, 0);
    memset(&aout, 0, sizeof aout);
    sink += CALL4(wia_unicodestringtoutf8string, &aout, &uin, 1, 0);
    ain.Buffer = u8big; ain.Length = 40000; ain.MaximumLength = 40000;
    uout.Buffer = owbuf; uout.Length = 0; uout.MaximumLength = 0xFFFF;
    sink += CALL4(wia_utf8stringtounicodestring, &uout, &ain, 0, 0);
    memset(&uout, 0, sizeof uout);
    sink += CALL4(wia_utf8stringtounicodestring, &uout, &ain, 1, 0);

    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_027) || defined(T_031)
#if defined(T_027)
#define NAME "027-rtlupcaseunicodetomultibyten"
extern long wia_u2umb(char*, unsigned long, unsigned long*, const wchar_t*, unsigned long);
extern void wia_upansimap_init(void);
#define UPFN   wia_u2umb
#define SETUP() wia_upansimap_init()
#else
#define NAME "031-rtlupcaseunicodetooemn"
extern long wia_u2uoem(char*, unsigned long, unsigned long*, const wchar_t*, unsigned long);
extern void wia_upoemmap_init(void);
#define UPFN   wia_u2uoem
#define SETUP() wia_upoemmap_init()
#endif
static void thunk(void){
    /* TWO PATHS THAT ALTERNATE ON THE DATA, and both have to be reached: a 16-wide and an 8-wide
       ASCII block that upcase in-register, and a table walk for anything above 0x7F. The corpus
       below is the one that found these two functions running at 0.59x -- pure ASCII, pure
       non-ASCII, and the two INTERLEAVED, which is what makes the block and the table hand over to
       each other repeatedly inside a single call.

       The truncating capacities matter separately: this function reports STATUS_BUFFER_OVERFLOW and
       stops part-way, so the exit it takes on a short destination is a different exit.

       Armed PER CALL (CALL5 -- five arguments), not around the thunk: a thunk that uses a register
       for its own loop hides an implementation that destroys it. */
    static wchar_t src[4096];
    static char dst[4096];
    static const int LENS[9] = { 0, 1, 7, 8, 9, 15, 16, 17, 4000 };
    unsigned long produced = 0;
    unsigned long long sink = 0;
    int i, k, cls;
    for (cls = 0; cls < 4; ++cls) {
        for (k = 0; k < 4096; ++k)
            src[k] = (wchar_t)(cls == 0 ? (0x61 + (k & 15))
                             : cls == 1 ? (0x0430 + (k % 26))
                             : cls == 2 ? (0x4E00 + (k % 512))
                                        : ((k & 1) ? 0x00E9 : (0x61 + (k & 15))));
        for (i = 0; i < 9; ++i) {
            unsigned long sb = (unsigned long)(LENS[i] * 2);
            sink += CALL5(UPFN, dst, (unsigned long)(LENS[i] + 8), &produced, src, sb);
            sink += produced;
            sink += CALL5(UPFN, dst, (unsigned long)(LENS[i] / 2), &produced, src, sb);
            sink += produced;
            sink += CALL5(UPFN, dst, 0, &produced, src, sb);   /* nothing fits at all */
            sink += produced;
        }
    }
    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_269)
#define NAME "269-convertstringsidtosid"
extern int wia_str2sid(const wchar_t*, void**);
extern int wia_sid_alias_init(void);
extern int wia_sid_classify_init(void);
/* Abort rather than run: a table that failed to build would turn every alias into a refusal, and
   the thunk would still "pass" having never entered the alias path. */
#define SETUP() do { if (wia_sid_classify_init() || wia_sid_alias_init()) {                    \
                         printf("ABI 269: the OS-derived tables failed to build\n");           \
                         ExitProcess(2); } } while (0)
static void thunk(void){
    /* SEVEN EXITS AND ALL OF THEM MATTER. This function returns through five different failure
       labels and two success paths, and three of the five CALL OUT -- to LocalAlloc through
       wia_sid_alloc, and to SetLastError through the three error setters -- so a register the
       implementation failed to save could be destroyed on one path and preserved on the others.

       The frame is 1080 bytes with seven registers pushed and nothing pushed in the body, which is
       the shape whose unwind data can be wrong without any test noticing.

       Armed PER CALL (CALL4), because a thunk that uses a register for its own loop hides an
       implementation that destroys it. Every allocated SID is freed. */
    static const wchar_t* CASES[] = {
        L"S-1-5-1",                                    /* the shortest success */
        L"S-1-5-21-305419896-2596069104-287454020-1001",   /* a real account SID */
        L"S-1-5-1-2-3-4-5-6-7-8-9-10",                 /* ten sub-authorities */
        L"S-0x1-5-1a2b-3c4d",                          /* the hexadecimal carry */
        L"S-1-\x0661\x0662-\x0967\x0968-\x0E51",     /* the Unicode digit path */
        L"BA",                                         /* the alias table */
        L"LA",                                         /* ... and a machine-specific one */
        L"ZZ",                                         /* not an alias: ERROR_INVALID_SID */
        L"S-1-5",                                      /* no sub-authority */
        L"S-256-5-1",                                  /* a revision above 255 */
        L"S-1-281474976710656-1",                      /* an authority above 48 bits */
        L"S-1-5-1)",                                   /* the SDDL terminator: clears the pointer */
        L"not-a-sid",                                  /* an immediate refusal */
        L""                                            /* empty */
    };
    static wchar_t manysub[2048];
    void* p;
    unsigned long long sink = 0;
    int i, n, k;

    for (i = 0; i < (int)(sizeof CASES / sizeof CASES[0]); ++i) {
        p = 0;
        sink += (unsigned)CALL4(wia_str2sid, CASES[i], &p, 0, 0);
        if (p) { LocalFree(p); }
    }
    /* and the ERROR_ARITHMETIC_OVERFLOW exit, which needs 255 sub-authorities to reach */
    n = 0;
    n += wsprintfW(manysub + n, L"S-1-5");
    for (k = 0; k < 255; ++k) n += wsprintfW(manysub + n, L"-%d", (k % 9) + 1);
    p = 0;
    sink += (unsigned)CALL4(wia_str2sid, manysub, &p, 0, 0);
    if (p) LocalFree(p);
    /* and the largest SID that IS accepted, which is the largest allocation and the longest copy */
    n = 0;
    n += wsprintfW(manysub + n, L"S-1-5");
    for (k = 0; k < 254; ++k) n += wsprintfW(manysub + n, L"-%d", (k % 9) + 1);
    p = 0;
    sink += (unsigned)CALL4(wia_str2sid, manysub, &p, 0, 0);
    if (p) LocalFree(p);
    /* the two NULL arguments */
    sink += (unsigned)CALL4(wia_str2sid, 0, &p, 0, 0);
    sink += (unsigned)CALL4(wia_str2sid, L"S-1-5-1", 0, 0, 0);

    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_067)
#define NAME "067-rtlconvertsidtounicodestring"
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } U067;
extern long wia_sidfmt(U067*, void*, unsigned char);
#define SETUP() ((void)0)
static void thunk(void){
    /* THE FRAME IS 552 BYTES WITH EIGHT REGISTERS PUSHED AND ONE CALL OUT, which is the shape whose
       unwind data can be wrong without any test noticing -- and the call out is what makes it
       matter, because wia_sid_header runs an exception handler and a mis-described frame is only
       visible when something unwinds through it.

       Four paths, and all four are driven: the ordinary format, the hexadecimal identifier
       authority (a different converter), the two refusals, and the destination-too-small refusal
       that formats everything first and then throws it away. The maximum-length case is included
       because it is the one that takes the 32-byte AVX2 copy loop rather than the 8-byte one.

       Armed PER CALL (CALL4), because a thunk that uses a register for its own loop hides an
       implementation that destroys it. */
    static unsigned char sid[8 + 4 * 16];
    static wchar_t buf[512];
    U067 u;
    unsigned long long sink = 0;
    int i, k;

    for (k = 0; k < 6; ++k) {
        static const unsigned char AUTH[6][6] = {
            {0,0,0,0,0,5}, {0,0,0,0,0,0}, {0,0,0,0,0,16},
            {0,0,255,255,255,255}, {1,0,0,0,0,0}, {255,255,255,255,255,255}
        };
        for (i = 0; i < 6; ++i) sid[2 + i] = AUTH[k][i];
        for (i = 0; i < 16; ++i) {
            unsigned v = (unsigned)(0x9E3779B9u * (unsigned)(i + k + 1));
            sid[8 + 4*i + 0] = (unsigned char)v;
            sid[8 + 4*i + 1] = (unsigned char)(v >> 8);
            sid[8 + 4*i + 2] = (unsigned char)(v >> 16);
            sid[8 + 4*i + 3] = (unsigned char)(v >> 24);
        }
        sid[0] = 1;
        for (i = 0; i <= 15; ++i) {
            sid[1] = (unsigned char)i;
            u.Length = 0; u.MaximumLength = sizeof buf; u.Buffer = buf;
            sink += (unsigned)CALL4(wia_sidfmt, &u, sid, 0, 0);
            /* ... and the same SID with barely enough room, and with none */
            u.Length = 0; u.MaximumLength = 12; u.Buffer = buf;
            sink += (unsigned)CALL4(wia_sidfmt, &u, sid, 0, 0);
            u.Length = 0; u.MaximumLength = 0; u.Buffer = buf;
            sink += (unsigned)CALL4(wia_sidfmt, &u, sid, 0, 0);
        }
    }
    /* the two refusals */
    sid[0] = 2; sid[1] = 5;
    u.Length = 0; u.MaximumLength = sizeof buf; u.Buffer = buf;
    sink += (unsigned)CALL4(wia_sidfmt, &u, sid, 0, 0);
    sid[0] = 1; sid[1] = 16;
    u.Length = 0; u.MaximumLength = sizeof buf; u.Buffer = buf;
    sink += (unsigned)CALL4(wia_sidfmt, &u, sid, 0, 0);
    sid[1] = 255;
    u.Length = 0; u.MaximumLength = sizeof buf; u.Buffer = buf;
    sink += (unsigned)CALL4(wia_sidfmt, &u, sid, 0, 0);

    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_270)
#define NAME "270-convertsidtostringsid"
extern int wia_sid2str(const void*, wchar_t**);
#define SETUP() ((void)0)
static void thunk(void){
    /* AN ENVELOPE OVER CHANGE 067, SO TWO FRAMES ARE UNDER TEST AT ONCE: this one's 856 bytes with
       four registers pushed, and 067's 552 with eight, nested inside it -- and 067's body calls out
       to an exception handler of its own. A register that either of them failed to save is only
       visible from out here.

       Five exits: the success, the two refusals that come back from the formatter, the NULL-argument
       refusal, and the allocation failure that cannot be provoked. Three of the five call out, to
       LocalAlloc and to SetLastError.

       Every allocated block is freed. Armed PER CALL (CALL4), because a thunk that uses a register
       for its own loop hides an implementation that destroys it. */
    static unsigned char sid[8 + 4 * 16];
    wchar_t* p;
    unsigned long long sink = 0;
    int i, k;

    for (k = 0; k < 4; ++k) {
        static const unsigned char AUTH[4][6] = {
            {0,0,0,0,0,5}, {0,0,0,0,0,0},
            {0,0,255,255,255,255},          /* the last decimal authority */
            {255,255,255,255,255,255}       /* the longest hexadecimal one */
        };
        for (i = 0; i < 6; ++i) sid[2 + i] = AUTH[k][i];
        for (i = 0; i < 16; ++i) {
            unsigned v = (unsigned)(0x9E3779B9u * (unsigned)(i + k + 1));
            sid[8 + 4*i + 0] = (unsigned char)v;
            sid[8 + 4*i + 1] = (unsigned char)(v >> 8);
            sid[8 + 4*i + 2] = (unsigned char)(v >> 16);
            sid[8 + 4*i + 3] = (unsigned char)(v >> 24);
        }
        sid[0] = 1;
        for (i = 0; i <= 15; ++i) {
            sid[1] = (unsigned char)i;
            p = 0;
            sink += (unsigned)CALL4(wia_sid2str, sid, &p, 0, 0);
            if (p) LocalFree(p);
        }
    }
    /* the refusals, and the two NULL arguments */
    sid[0] = 2; sid[1] = 5;  p = 0; sink += (unsigned)CALL4(wia_sid2str, sid, &p, 0, 0); if (p) LocalFree(p);
    sid[0] = 1; sid[1] = 16; p = 0; sink += (unsigned)CALL4(wia_sid2str, sid, &p, 0, 0); if (p) LocalFree(p);
    sid[1] = 255;            p = 0; sink += (unsigned)CALL4(wia_sid2str, sid, &p, 0, 0); if (p) LocalFree(p);
    p = 0; sink += (unsigned)CALL4(wia_sid2str, 0, &p, 0, 0);
    sid[1] = 5;              sink += (unsigned)CALL4(wia_sid2str, sid, 0, 0, 0);

    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_271)
#define NAME "271-convertsidtostringsida"
extern int wia_sid2stra(const void*, char**);
#define SETUP() ((void)0)
static void thunk(void){
    /* THE SAME TWO NESTED FRAMES AS 270 -- this one's 856 bytes with four pushes over change 067's
       552 with eight, whose body calls out to an exception handler -- plus a VECTOR pack, which is
       the part that matters here: VPACKUSWB writes xmm0 and xmm1, and the LOW 128 BITS of xmm6 to
       xmm15 are non-volatile. Sixteen implementations in this repository used an xmm register as
       scratch undetected for months, and the only reason it was ever found is that change 202's
       benchmark keeps its accumulators in xmm6/xmm7 and reported a correct function as taking
       0.00 ns.

       The counts are driven 0..15 against four identifier authorities so that the result length
       crosses the sixteen-character pack boundary in both directions -- the byte loop below sixteen
       and the overlapping tail above it.

       Armed PER CALL (CALL4). Every allocated block is freed. */
    static unsigned char sid[8 + 4 * 16];
    char* p;
    unsigned long long sink = 0;
    int i, k;

    for (k = 0; k < 4; ++k) {
        static const unsigned char AUTH[4][6] = {
            {0,0,0,0,0,5}, {0,0,0,0,0,0},
            {0,0,255,255,255,255}, {255,255,255,255,255,255}
        };
        for (i = 0; i < 6; ++i) sid[2 + i] = AUTH[k][i];
        for (i = 0; i < 16; ++i) {
            unsigned v = (unsigned)(0x9E3779B9u * (unsigned)(i + k + 1));
            if (k == 1) v = (unsigned)(i + 1);          /* short numbers: short results */
            sid[8 + 4*i + 0] = (unsigned char)v;
            sid[8 + 4*i + 1] = (unsigned char)(v >> 8);
            sid[8 + 4*i + 2] = (unsigned char)(v >> 16);
            sid[8 + 4*i + 3] = (unsigned char)(v >> 24);
        }
        sid[0] = 1;
        for (i = 0; i <= 15; ++i) {
            sid[1] = (unsigned char)i;
            p = 0;
            sink += (unsigned)CALL4(wia_sid2stra, sid, &p, 0, 0);
            if (p) LocalFree(p);
        }
    }
    sid[0] = 2; sid[1] = 5;  p = 0; sink += (unsigned)CALL4(wia_sid2stra, sid, &p, 0, 0); if (p) LocalFree(p);
    sid[0] = 1; sid[1] = 16; p = 0; sink += (unsigned)CALL4(wia_sid2stra, sid, &p, 0, 0); if (p) LocalFree(p);
    sid[1] = 255;            p = 0; sink += (unsigned)CALL4(wia_sid2stra, sid, &p, 0, 0); if (p) LocalFree(p);
    p = 0; sink += (unsigned)CALL4(wia_sid2stra, 0, &p, 0, 0);
    sid[1] = 5;              sink += (unsigned)CALL4(wia_sid2stra, sid, 0, 0, 0);

    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_272)
#define NAME "272-convertstringsidtosida"
extern int wia_str2sida(const char*, void**);
extern int wia_sid_alias_init(void);
extern int wia_sid_classify_init(void);
#define SETUP() do { if (wia_sid_classify_init() || wia_sid_alias_init()) {                    \
                         printf("ABI 272: the OS-derived tables failed to build\n");           \
                         ExitProcess(2); } } while (0)
static void thunk(void){
    /* TWO NESTED FRAMES AND A VECTOR SCAN. This one is 2104 bytes with six registers pushed, over
       change 269's 1080 with seven -- and between them sits an AVX2 scan that finds the length and
       the "any byte at or above 0x80" answer in one pass. The LOW 128 BITS of xmm6 to xmm15 are
       non-volatile; sixteen implementations in this repository used an xmm register as scratch
       undetected for months.

       Both widening paths are driven, because they are different code: pure ASCII takes a
       VPMOVZXBW zero extension and anything with a high byte calls MultiByteToWideChar. Both the
       stack temporary and the ALLOCATED one are driven, because the allocation has its own exit and
       its own free. Alignment is swept 0..63, since the scan's first block is loaded aligned down.

       Armed PER CALL (CALL4). Every allocated SID is freed. */
    static char buf[8192];
    static const char* CASES[] = {
        "S-1-5-1",
        "S-1-5-21-305419896-2596069104-287454020-1001",
        "BA", "LA", "ZZ",
        "S-0x1-5-1a2b-3c4d",
        "S-1-5-1)",                                   /* clears the output pointer */
        "S-1-\xE9\xE9-5-1",                           /* a high byte: the code-page fallback */
        "\x80\x81\x82\x83",
        "not-a-sid",
        ""
    };
    void* p;
    unsigned long long sink = 0;
    int i, off, k, n;

    for (i = 0; i < (int)(sizeof CASES / sizeof CASES[0]); ++i) {
        for (off = 0; off < 64; off += 7) {
            char* q = buf + off;
            for (k = 0; CASES[i][k]; ++k) q[k] = CASES[i][k];
            q[k] = 0;
            p = 0;
            sink += (unsigned)CALL4(wia_str2sida, q, &p, 0, 0);
            if (p) LocalFree(p);
        }
    }
    /* the ALLOCATED temporary: past 1022 characters the widened copy is not the frame */
    n = wsprintfA(buf, "S-1-5");
    while (n < 1000) n += wsprintfA(buf + n, "-%09d", (n % 9) + 1);
    p = 0; sink += (unsigned)CALL4(wia_str2sida, buf, &p, 0, 0); if (p) LocalFree(p);
    buf[600] = (char)0xE9;                            /* ... and with a high byte in it */
    p = 0; sink += (unsigned)CALL4(wia_str2sida, buf, &p, 0, 0); if (p) LocalFree(p);
    for (k = 0; k < 4000; ++k) buf[k] = (char)('a' + (k % 26));
    buf[4000] = 0;
    p = 0; sink += (unsigned)CALL4(wia_str2sida, buf, &p, 0, 0); if (p) LocalFree(p);
    /* the two NULL arguments */
    p = 0; sink += (unsigned)CALL4(wia_str2sida, 0, &p, 0, 0);
    sink += (unsigned)CALL4(wia_str2sida, "S-1-5-1", 0, 0, 0);

    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_273)
#define NAME "273-inet-addr"
extern unsigned long wia_inet_addr(const char*);
#define SETUP() ((void)0)
static void thunk(void){
    /* A LEAF WITH A FRAME AND NO CALLS, which is the shape whose unwind data nobody checks because
       nothing ever unwinds through it -- until something does. Every path is driven: the four
       forms, the three bases, the wrapping accumulator, the whitespace terminator, the one-byte
       special case, the five refusals, and NULL.

       Armed PER CALL (CALL4), because a thunk that uses a register for its own loop hides an
       implementation that destroys it. */
    static const char* CASES[] = {
        "1.2.3.4", "192.168.100.200", "255.255.255.254", "0.0.0.0",
        "1.2.3", "1.2", "16909060",
        "0x7f000001", "0X7F000001", "017700000001", "0x1.0x2.0x3.0x4", "1.0x2.03.4",
        "0x112345678",                 /* the wrapping accumulator */
        "12345678901",                 /* ... in decimal */
        "0x212345678",                 /* ... and one that goes down: refused */
        "1.2.3.4 and trailing text",   /* whitespace ends it */
        "1\t2", " ",                   /* the one-byte special case */
        "1.2.3.256", "256.1.1.1", "1.1.65536", "0x", "08", "not-an-address", "",
        "0000000000000000000000000001.2.3.4"
    };
    unsigned long long sink = 0;
    int i;
    for (i = 0; i < (int)(sizeof CASES / sizeof CASES[0]); ++i)
        sink += (unsigned)CALL4(wia_inet_addr, CASES[i], 0, 0, 0);
    sink += (unsigned)CALL4(wia_inet_addr, 0, 0, 0, 0);
    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_275)
#define NAME "275-inet-ntoa"
extern char* wia_inet_ntoa(unsigned long);
#define SETUP() ((void)0)
static void thunk(void){
    /* A 32-byte frame with one register pushed and ONE CALL OUT -- to the thread-local buffer, which
       the compiler reaches through gs:[0x58] and the TLS array. That call is the reason the unwind
       data matters here: a mis-described frame is only visible when something unwinds through it,
       and a TLS access on a thread whose slot has not been materialised yet can do exactly that.

       Every field length and every mixture of them is driven, because the implementation steps by
       two, three or four bytes per field and a thunk that only formatted 127.0.0.1 would exercise
       one step length three times.

       Armed PER CALL (CALL4). */
    static const unsigned long V[] = {
        0x01010101ul, 0x0A0A0A0Aul, 0xFFFFFFFFul, 0x00000000ul,
        0x0100007Ful, 0xC8A8A8C0ul, 0x0101A8C0ul, 0x64020A0Aul,
        0x63636363ul, 0x64646464ul, 0x09090909ul, 0x0A090A09ul,
        0xFF0000FFul, 0x00FFFF00ul
    };
    unsigned long long sink = 0;
    int i;
    for (i = 0; i < (int)(sizeof V / sizeof V[0]); ++i)
        sink += (unsigned)CALL4(wia_inet_ntoa, V[i], 0, 0, 0);
    /* and every byte value in the low field, so every table entry is touched */
    for (i = 0; i < 256; ++i)
        sink += (unsigned)CALL4(wia_inet_ntoa, (unsigned long)i | 0x01010100ul, 0, 0, 0);
    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_277)
#define NAME "277-charupperbuffw"
extern unsigned long wia_charupperbuffw(wchar_t*, unsigned long);
extern unsigned long wia_charlowerbuffw(wchar_t*, unsigned long);
extern int wia_cub_init(void);
#define SETUP() do { if (wia_cub_init()) {                                                     \
                         printf("ABI 277: the case tables failed to build\n");                 \
                         ExitProcess(2); } } while (0)
static void thunk(void){
    /* BOTH EXPORTS AND BOTH PATHS. A 16-character block with no code unit at or above 0x80 is
       handled entirely in YMM registers and everything else falls back to a table, so a thunk of
       plain ASCII would leave the table path's register use untested -- and the LOW 128 BITS of
       xmm6 to xmm15 are non-volatile, which is what sixteen implementations in this repository got
       wrong undetected for months.

       Lengths are driven across the 32-byte block boundary in both directions, plus the count-0
       case, which must touch nothing.

       Armed PER CALL (CALL4). */
    static wchar_t buf[4096];
    unsigned long long sink = 0;
    int i, n;

    for (n = 0; n <= 80; ++n) {
        for (i = 0; i < n; ++i) buf[i] = (wchar_t)('a' + (i % 26));
        sink += CALL4(wia_charupperbuffw, buf, n, 0, 0);
        sink += CALL4(wia_charlowerbuffw, buf, n, 0, 0);
        /* the same lengths with a high code unit, which forces the table path */
        for (i = 0; i < n; ++i) buf[i] = (wchar_t)(0x0100 + (i % 0x200));
        sink += CALL4(wia_charupperbuffw, buf, n, 0, 0);
        sink += CALL4(wia_charlowerbuffw, buf, n, 0, 0);
        /* and one high code unit among ASCII: one block falls back, the rest do not */
        for (i = 0; i < n; ++i) buf[i] = (wchar_t)('a' + (i % 26));
        if (n) buf[n / 2] = (wchar_t)0x00E9;
        sink += CALL4(wia_charupperbuffw, buf, n, 0, 0);
        sink += CALL4(wia_charlowerbuffw, buf, n, 0, 0);
    }
    for (i = 0; i < 4000; ++i) buf[i] = (wchar_t)('A' + (i % 26));
    sink += CALL4(wia_charupperbuffw, buf, 4000, 0, 0);
    sink += CALL4(wia_charlowerbuffw, buf, 4000, 0, 0);

    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_278)
#define NAME "278-rtlintegertounicodestring"
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } U278;
extern long wia_int2ustr(unsigned long, unsigned long, U278*);
#define SETUP() ((void)0)
static void thunk(void){
    /* A LEAF WITH NO FRAME AND NO CALLS -- the shape whose unwind data nobody checks because
       nothing ever unwinds through it, until something does.

       TWO CONVERTERS ARE UNDER TEST, not one: base 10 goes through a length-first,
       two-digits-at-a-time path and bases 2, 8 and 16 share a shift-and-mask loop. A thunk of base
       10 alone would leave half the register use untested. Both refusals are driven too, because
       they return before either converter runs.

       Armed PER CALL (CALL4). */
    static wchar_t buf[64];
    static const unsigned long BASES[] = { 0, 2, 8, 10, 16, 7, 36 };
    static const unsigned long VALUES[] = {
        0ul, 1ul, 9ul, 10ul, 255ul, 256ul, 65535ul, 65536ul,
        3735928559ul, 2147483647ul, 2147483648ul, 4294967295ul
    };
    U278 u;
    unsigned long long sink = 0;
    int i, j;
    unsigned short m;

    for (i = 0; i < (int)(sizeof BASES / sizeof BASES[0]); ++i)
        for (j = 0; j < (int)(sizeof VALUES / sizeof VALUES[0]); ++j) {
            u.Length = 0; u.MaximumLength = sizeof buf; u.Buffer = buf;
            sink += (unsigned)CALL4(wia_int2ustr, VALUES[j], BASES[i], &u, 0);
            /* and with room exactly at the boundary, and with none at all */
            for (m = 0; m <= 70; m += 7) {
                u.Length = 0; u.MaximumLength = m; u.Buffer = buf;
                sink += (unsigned)CALL4(wia_int2ustr, VALUES[j], BASES[i], &u, 0);
            }
        }
    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_279)
#define NAME "279-rtlintegertochar"
extern long wia_int2char(unsigned long, unsigned long, long, char*);
#define SETUP() ((void)0)
static void thunk(void){
    /* A LEAF WITH NO FRAME AND NO CALLS -- the shape whose unwind data nobody checks because
       nothing ever unwinds through it, until something does.

       THREE WRITE PATHS ARE UNDER TEST, not one. Base 10 goes through a length-first,
       two-digits-at-a-time converter; bases 2, 8 and 16 share a shift-and-mask loop; and a NEGATIVE
       length runs a zero-padding fill that no positive length ever reaches. That fill is the only
       part of this change that touches an XMM register, so a thunk of positive lengths alone would
       leave the one vector register the implementation uses entirely undriven.

       Every padding size class is driven too -- 1, 2, 3, 4..7, 8..15, 16..31 and the 32-byte loop --
       because they are separate blocks with separate register use and the wide ones write through
       an index the small ones do not.

       Both refusals are driven, because they return before any converter runs.

       Armed PER CALL (CALL4). */
    static char buf[512];
    static const unsigned long BASES[] = { 0, 2, 8, 10, 16, 7, 36 };
    static const unsigned long VALUES[] = {
        0ul, 1ul, 9ul, 10ul, 255ul, 256ul, 65535ul, 65536ul,
        3735928559ul, 2147483647ul, 2147483648ul, 4294967295ul
    };
    unsigned long long sink = 0;
    int i, j;
    long k;

    for (i = 0; i < (int)(sizeof BASES / sizeof BASES[0]); ++i)
        for (j = 0; j < (int)(sizeof VALUES / sizeof VALUES[0]); ++j) {
            sink += (unsigned)CALL4(wia_int2char, VALUES[j], BASES[i], 400, buf);
            /* every length from none at all to well past the answer, positive and negative, so
               that each padding size class and each refusal is armed in turn */
            for (k = 0; k <= 70; ++k) {
                sink += (unsigned)CALL4(wia_int2char, VALUES[j], BASES[i], k, buf);
                sink += (unsigned)CALL4(wia_int2char, VALUES[j], BASES[i], -k, buf);
            }
            sink += (unsigned)CALL4(wia_int2char, VALUES[j], BASES[i], -200, buf);
            sink += (unsigned)CALL4(wia_int2char, VALUES[j], BASES[i], -400, buf);
            sink += (unsigned)CALL4(wia_int2char, VALUES[j], BASES[i], (long)0x80000000ul, buf);
        }
    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_280)
#define NAME "280-rtllargeintegertochar"
typedef struct { long long q; } LI280;
extern long wia_lint2char(const LI280*, unsigned long, long, char*);
#define SETUP() ((void)0)
static void thunk(void){
    /* A LEAF WITH NO FRAME, NO PUSHES AND NO CALLS -- the shape whose unwind data nobody checks
       because nothing ever unwinds through it, until something does.

       FIVE PATHS ARE UNDER TEST, not one:
         * base 10 above 2^32, which peels EIGHT digits at a time through the 64-bit reciprocal;
         * base 10 below 2^32, which never enters that peel;
         * the ONE-DIGIT decimal path, which writes its character and returns without touching a
           table, a dispatch or the room rule;
         * bases 2, 8 and 16, which emit several digits per store from three different tables --
           base 2 running to SIXTY-FOUR characters, twice what change 279 could produce;
         * the ZERO-PADDED FIELD FILL, which is the only place this change touches an XMM register.

       Every padding size class is driven -- 1, 2, 3, 4..7, 8..15, 16..31 and the 32-byte loop --
       because they are separate blocks with separate register use, and the wide ones write through
       an index the small ones do not.

       Both refusals are driven, because they return before any converter runs, and INT_MIN is
       driven because it is the one negative length that refuses.

       Armed PER CALL (CALL4). */
    static char buf[512];
    static const unsigned long BASES[] = { 0, 2, 8, 10, 16, 7, 36 };
    static const long long VALUES[] = {
        0ll, 1ll, 9ll, 10ll, 255ll, 65536ll,
        4294967295ll, 4294967296ll,                    /* either side of the peel boundary */
        99999999ll, 100000000ll,                       /* either side of 10^8 itself */
        1234567890123456789ll,
        (long long)0x8000000000000000ull,
        (long long)0xFFFFFFFFFFFFFFFFull
    };
    LI280 v;
    unsigned long long sink = 0;
    int i, j;
    long k;

    for (i = 0; i < (int)(sizeof BASES / sizeof BASES[0]); ++i)
        for (j = 0; j < (int)(sizeof VALUES / sizeof VALUES[0]); ++j) {
            v.q = VALUES[j];
            sink += (unsigned)CALL4(wia_lint2char, &v, BASES[i], 400, buf);
            /* every length from none at all to past the longest answer, positive and negative, so
               that each padding size class and each refusal is armed in turn */
            for (k = 0; k <= 100; ++k) {
                sink += (unsigned)CALL4(wia_lint2char, &v, BASES[i], k, buf);
                sink += (unsigned)CALL4(wia_lint2char, &v, BASES[i], -k, buf);
            }
            sink += (unsigned)CALL4(wia_lint2char, &v, BASES[i], -300, buf);
            sink += (unsigned)CALL4(wia_lint2char, &v, BASES[i], -500, buf);
            sink += (unsigned)CALL4(wia_lint2char, &v, BASES[i], (long)0x80000000ul, buf);
        }
    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_281)
#define NAME "281-strchriw"
extern const wchar_t* wia_strchriw(const wchar_t*, wchar_t);
int wia_sci_init(void);
extern unsigned char wia_sci_n[65536];
#define SETUP() do { if (wia_sci_init()) { printf("ABI: table init failed\n"); return 1; } } while (0)
static void thunk(void){
    /* A LEAF WITH NO FRAME AND NO CALLS, and the one in this repository with the most to lose from
       a register slip: it is the first change to use FOUR YMM registers for live data across a
       loop. Win64 makes xmm6-xmm15 non-volatile, and the first draft of impl.asm used ymm6 and
       ymm7 as scratch -- which is exactly what this gate exists to catch.

       ALL THREE DISPATCH PATHS ARE DRIVEN, because they use different registers:
         * a needle matching only itself      -> one broadcast replicated four times
         * a needle with 2..4 partners        -> four broadcasts loaded from the pool
         * a needle with 5..8 partners        -> the inline scalar list, no YMM at all
         * a needle with more than eight      -> the bitmap path, no YMM at all
       and both exits that touched a YMM register, which must VZEROUPPER, plus the two refusals
       that return before any YMM is touched and must NOT.

       Armed PER CALL (CALL4). */
    static wchar_t buf[600];
    unsigned long long sink = 0;
    int i, k;
    unsigned selfonly = 0, small = 0, mid = 0, big = 0;

    for (i = 0; i < 599; ++i) buf[i] = (wchar_t)(L'a' + (i % 8));
    buf[599] = 0;

    /* pick one needle of each shape from the same table the implementation dispatches on */
    for (i = 1; i < 0xFFFF; ++i) {
        unsigned n = wia_sci_n[i];
        if (!selfonly && n == 0 && i > 0x3000) selfonly = (unsigned)i;
        if (!small && n >= 2 && n <= 4) small = (unsigned)i;
        if (!mid && n >= 5 && n <= 8) mid = (unsigned)i;
        if (!big && n == 255) big = (unsigned)i;
    }

    for (k = 0; k < 4; ++k) {
        const wchar_t* p = buf + k;              /* every alignment class of the aligned prologue */
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strchriw, p, (wchar_t)selfonly, 0, 0);
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strchriw, p, (wchar_t)small, 0, 0);
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strchriw, p, (wchar_t)mid, 0, 0);
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strchriw, p, (wchar_t)big, 0, 0);
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strchriw, p, L'c', 0, 0);   /* a hit */
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strchriw, p, L'#', 0, 0);   /* a miss */
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strchriw, p, 0, 0, 0);      /* refusal */
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strchriw, 0, L'a', 0, 0);   /* refusal */
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strchriw, L"", L'a', 0, 0);
    }
    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_282)
#define NAME "282-strrchriw"
extern const wchar_t* wia_strrchriw(const wchar_t*, const wchar_t*, wchar_t);
int wia_sci_init(void);
extern unsigned char wia_sci_n[65536];
#define SETUP() do { if (wia_sci_init()) { printf("ABI: table init failed\n"); return 1; } } while (0)
static void thunk(void){
    /* A LEAF WITH NO FRAME AND NO CALLS that keeps FOUR YMM registers live across a loop. Win64
       makes xmm6-xmm15 non-volatile; change 281's first draft used ymm6 and ymm7 as scratch, which
       is what this gate exists to catch, and this change inherits that register budget.

       ALL FOUR DISPATCH SHAPES ARE DRIVEN, because they use different registers: one broadcast
       replicated, four broadcasts loaded from the pool, an inline scalar list, and a bitmap -- the
       last two touching no YMM at all.

       BOTH EDGE MASKS ARE DRIVEN, including the case where the whole range lives inside ONE 32-byte
       block and both masks apply at once, which is the shape a mask written for two separate blocks
       gets wrong.

       And the exits: two that touched a YMM register and must VZEROUPPER, and the refusals that
       return before any YMM is touched and must not.

       Armed PER CALL (CALL4). */
    static wchar_t buf[600];
    unsigned long long sink = 0;
    int i, k, n;
    unsigned selfonly = 0, small = 0, mid = 0, big = 0;

    for (i = 0; i < 600; ++i) buf[i] = (wchar_t)(L'a' + (i % 8));

    for (i = 1; i < 0xFFFF; ++i) {
        unsigned q = wia_sci_n[i];
        if (!selfonly && q == 0 && i > 0x3000) selfonly = (unsigned)i;
        if (!small && q >= 2 && q <= 4) small = (unsigned)i;
        if (!mid && q >= 5 && q <= 8) mid = (unsigned)i;
        if (!big && q == 255) big = (unsigned)i;
    }

    for (k = 0; k < 4; ++k) {                    /* every start alignment class */
        const wchar_t* p = buf + k;
        for (n = 1; n <= 40; n += 13) {          /* ranges inside one block and across several */
            const wchar_t* e = p + n;
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strrchriw, p, e, (wchar_t)selfonly, 0);
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strrchriw, p, e, (wchar_t)small, 0);
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strrchriw, p, e, (wchar_t)mid, 0);
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strrchriw, p, e, (wchar_t)big, 0);
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strrchriw, p, e, L'c', 0);
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strrchriw, p, e, L'#', 0);
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strrchriw, p, e, 0, 0);
        }
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strrchriw, p, p, L'a', 0);      /* empty */
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strrchriw, 0, 0, L'a', 0);      /* null */
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strrchriw, p + 8, p, L'a', 0);  /* inverted */
    }
    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_283)
#define NAME "283-strrstriw"
extern const wchar_t* wia_strrstriw(const wchar_t*, const wchar_t*, const wchar_t*);
int wia_sci_init(void);
extern unsigned char wia_sci_n[65536];
#define SETUP() do { if (wia_sci_init()) { printf("ABI: table init failed\n"); return 1; } } while (0)
static void thunk(void){
    /* THE FIRST CHANGE IN THIS REPOSITORY WITH A REAL FRAME AND SEVEN SAVED REGISTERS.
       The character searches were leaves that saved nothing; this one pushes r15, r14, r13, r12,
       rbx, rsi and rdi, declares them with .pushreg, and calls two internal routines. Every one of
       those has to come back unchanged, and the two internal calls must not disturb them either.

       ALL THE PATHS ARE DRIVEN: the vector filter (a needle whose first character has 1..4
       partners), the WIDE filter (first character with more than four, which bypasses the vector
       scan entirely), the last-character early reject, needles of one character and of many, a
       needle longer than the haystack, an empty needle, an empty range, and every NULL argument.

       Armed PER CALL (CALL4). */
    static wchar_t hay[600];
    static wchar_t need[16];
    unsigned long long sink = 0;
    int i, k, n;
    unsigned selfonly = 0, wide = 0;

    for (i = 0; i < 599; ++i) hay[i] = (wchar_t)(L'a' + (i % 8));
    hay[599] = 0;

    for (i = 1; i < 0xFFFF; ++i) {
        unsigned q = wia_sci_n[i];
        if (!selfonly && q == 0 && i > 0x3000) selfonly = (unsigned)i;
        if (!wide && q == 255) wide = (unsigned)i;
    }

    for (k = 0; k < 4; ++k) {
        const wchar_t* p = hay + k;
        for (n = 1; n <= 6; ++n) {
            int j;
            for (j = 0; j < n; ++j) need[j] = (wchar_t)(L'a' + (j % 8));
            need[n] = 0;
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strrstriw, p, p + 100, need, 0);
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strrstriw, p, p + 3, need, 0);
        }
        /* a needle whose first character takes the WIDE path, and one that matches only itself */
        need[0] = (wchar_t)wide;  need[1] = L'b'; need[2] = 0;
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strrstriw, p, p + 100, need, 0);
        need[0] = (wchar_t)selfonly;
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strrstriw, p, p + 100, need, 0);
        /* degenerate shapes */
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strrstriw, p, p, L"a", 0);
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strrstriw, p, p + 100, L"", 0);
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strrstriw, 0, 0, L"a", 0);
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strrstriw, p, p + 100, 0, 0);
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strrstriw, p, p + 2, L"abcdefghij", 0);
    }
    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_284)
#define NAME "284-strstriw"
extern const wchar_t* wia_strstriw(const wchar_t*, const wchar_t*);
int wia_sci_init(void);
extern unsigned char wia_sci_n[65536];
#define SETUP() do { if (wia_sci_init()) { printf("ABI: table init failed\n"); return 1; } } while (0)
static void thunk(void){
    /* The FORWARD substring search. Like change 283 this has a real frame with seven saved
       registers and internal calls, but it has one more of them: a vectorised terminator scan
       (wterm) that runs before the filter is set up, on top of match_pair and vscan. Three internal
       routines, all of which must leave the seven saved registers alone.

       ALL THE PATHS ARE DRIVEN: the vector filter (a first character with 1..4 partners), the WIDE
       filter (more than four, bypassing the vector scan), the last-character early reject, region B
       (a needle whose TAIL matches a NUL, so the match runs past the terminator), a needle longer
       than the whole string, an empty needle, an empty string, and both NULL arguments.

       Armed PER CALL (CALL4). */
    static wchar_t hay[600];
    static wchar_t need[16];
    unsigned long long sink = 0;
    int i, k, n;
    unsigned selfonly = 0, wide = 0;

    for (i = 0; i < 599; ++i) hay[i] = (wchar_t)(L'a' + (i % 8));
    hay[599] = 0;

    for (i = 1; i < 0xFFFF; ++i) {
        unsigned q = wia_sci_n[i];
        if (!selfonly && q == 0 && i > 0x3000) selfonly = (unsigned)i;
        if (!wide && q == 255) wide = (unsigned)i;
    }

    for (k = 0; k < 4; ++k) {
        const wchar_t* p = hay + k;
        for (n = 1; n <= 6; ++n) {
            int j;
            for (j = 0; j < n; ++j) need[j] = (wchar_t)(L'a' + (j % 8));
            need[n] = 0;
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strstriw, p, need, 0, 0);
        }
        /* the WIDE path, and a first character that matches only itself */
        need[0] = (wchar_t)wide;  need[1] = L'b'; need[2] = 0;
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strstriw, p, need, 0, 0);
        need[0] = (wchar_t)selfonly;
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strstriw, p, need, 0, 0);
        /* REGION B: a tail of soft hyphens, every one of which matches a NUL, so the match runs
           past the terminator and the scalar clamped path runs */
        need[0] = L'H'; need[1] = 0x00AD; need[2] = 0x00AD; need[3] = 0;
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strstriw, p, need, 0, 0);
        /* degenerate shapes */
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strstriw, p, L"", 0, 0);
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strstriw, L"", L"a", 0, 0);
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strstriw, 0, L"a", 0, 0);
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strstriw, p, 0, 0, 0);
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strstriw, L"ab", L"abcdefghij", 0, 0);
    }
    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_285)
#define NAME "285-strcspniw"
extern int wia_strcspniw(const wchar_t*, const wchar_t*);
int wia_sci_init(void);
extern unsigned char wia_sci_n[65536];
#define SETUP() do { if (wia_sci_init()) { printf("ABI: table init failed\n"); return 1; } } while (0)
static void thunk(void){
    /* The SET span. This is the first change here to combine a real frame, seven saved registers, a
       576-BYTE STACK ALLOCATION declared with .allocstack, and THREE internal routines -- so the gate
       is checking that the stack pointer comes back exactly as well as the registers.

       ALL THE PATHS ARE DRIVEN: a one-character set (a single unbounded pass, no windowing), a set
       that expands past four accept entries (the doubling windows), a set large enough to need many
       chunks, a set whose member carries the 255 bitmap sentinel (the call-free scalar path, bitmap
       loop), a set member with no partners (the self loop) and one with 2..8 (the pool loop), an
       empty set, an empty string, and both NULL arguments.

       Armed PER CALL (CALL4). */
    static wchar_t str[600];
    static wchar_t set[40];
    unsigned long long sink = 0;
    int i, k, n;
    unsigned selfonly = 0, big = 0, mid = 0;

    for (i = 0; i < 599; ++i) str[i] = (wchar_t)(L'a' + (i % 8));
    str[599] = 0;

    for (i = 1; i < 0xFFFF; ++i) {
        unsigned q = wia_sci_n[i];
        if (!selfonly && q == 0 && i > 0x3000) selfonly = (unsigned)i;
        if (!big && q == 255) big = (unsigned)i;
        if (!mid && q >= 5 && q <= 8) mid = (unsigned)i;
    }

    for (k = 0; k < 4; ++k) {
        const wchar_t* p = str + k;

        /* one character: the single-pass path */
        set[0] = L'Q'; set[1] = 0;
        sink += (unsigned long long)(unsigned)CALL4(wia_strcspniw, p, set, 0, 0);
        set[0] = L'C';
        sink += (unsigned long long)(unsigned)CALL4(wia_strcspniw, p, set, 0, 0);

        /* growing sets: four accept entries, then past them into the windows and many chunks */
        for (n = 1; n <= 20; ++n) {
            for (i = 0; i < n; ++i) set[i] = (wchar_t)(L'M' + i);
            set[n] = 0;
            sink += (unsigned long long)(unsigned)CALL4(wia_strcspniw, p, set, 0, 0);
        }

        /* the scalar path: the 255 bitmap sentinel, alone and mixed with ordinary members */
        set[0] = (wchar_t)big; set[1] = 0;
        sink += (unsigned long long)(unsigned)CALL4(wia_strcspniw, p, set, 0, 0);
        set[0] = L'M'; set[1] = (wchar_t)big; set[2] = L'N'; set[3] = 0;
        sink += (unsigned long long)(unsigned)CALL4(wia_strcspniw, p, set, 0, 0);
        /* the self loop and the pool loop of the scalar path */
        set[0] = (wchar_t)selfonly; set[1] = (wchar_t)big; set[2] = 0;
        sink += (unsigned long long)(unsigned)CALL4(wia_strcspniw, p, set, 0, 0);
        set[0] = (wchar_t)mid; set[1] = (wchar_t)big; set[2] = 0;
        sink += (unsigned long long)(unsigned)CALL4(wia_strcspniw, p, set, 0, 0);

        /* degenerate shapes */
        sink += (unsigned long long)(unsigned)CALL4(wia_strcspniw, p, L"", 0, 0);
        sink += (unsigned long long)(unsigned)CALL4(wia_strcspniw, L"", L"a", 0, 0);
        sink += (unsigned long long)(unsigned)CALL4(wia_strcspniw, 0, L"a", 0, 0);
        sink += (unsigned long long)(unsigned)CALL4(wia_strcspniw, p, 0, 0, 0);
    }
    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_286)
#define NAME "286-strchrniw"
extern const wchar_t* wia_strchrniw(const wchar_t*, wchar_t, unsigned);
int wia_sci_init(void);
extern unsigned char wia_sci_n[65536];
#define SETUP() do { if (wia_sci_init()) { printf("ABI: table init failed\n"); return 1; } } while (0)
static void thunk(void){
    /* The count-bounded character search: a frame with seven saved registers and one internal routine.
       Fewer moving parts than 285, but the count makes the bound arithmetic new.

       ALL THE PATHS ARE DRIVEN: a character with no partners (the single-broadcast path), one with two
       to four (the four-register path), one with five to eight and one with the 255 sentinel (the two
       call-free wide loops), counts of zero, one, exactly the length, one short of a match, far past the
       terminator and 0xFFFFFFFF, an empty string and a NULL start.

       Armed PER CALL (CALL4). */
    static wchar_t str[600];
    unsigned long long sink = 0;
    int i, k;
    unsigned selfonly = 0, big = 0, mid = 0, small = 0;

    for (i = 0; i < 599; ++i) str[i] = (wchar_t)(L'a' + (i % 8));
    str[599] = 0;

    for (i = 1; i < 0xFFFF; ++i) {
        unsigned q = wia_sci_n[i];
        if (!selfonly && q == 0 && i > 0x3000) selfonly = (unsigned)i;
        if (!big && q == 255) big = (unsigned)i;
        if (!mid && q >= 5 && q <= 8) mid = (unsigned)i;
        if (!small && q >= 2 && q <= 4) small = (unsigned)i;
    }

    for (k = 0; k < 4; ++k) {
        const wchar_t* p = str + k;
        static const unsigned counts[] = { 0, 1, 2, 7, 16, 63, 64, 65, 511, 599, 600, 4096,
                                           0xFFFFFFFFu };
        int c;
        for (c = 0; c < (int)(sizeof(counts) / sizeof(counts[0])); ++c) {
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strchrniw, p, (wchar_t)L'C', counts[c], 0);
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strchrniw, p, (wchar_t)L'#', counts[c], 0);
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strchrniw, p, (wchar_t)selfonly, counts[c], 0);
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strchrniw, p, (wchar_t)small, counts[c], 0);
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strchrniw, p, (wchar_t)mid, counts[c], 0);
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strchrniw, p, (wchar_t)big, counts[c], 0);
            sink += (unsigned long long)(uintptr_t)CALL4(wia_strchrniw, p, (wchar_t)0, counts[c], 0);
        }
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strchrniw, L"", (wchar_t)L'a', 5, 0);
        sink += (unsigned long long)(uintptr_t)CALL4(wia_strchrniw, 0, (wchar_t)L'a', 5, 0);
    }
    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
}

#elif defined(T_287)
#define NAME "287-getstringtypew"
extern int wia_getstringtypew(unsigned long, const wchar_t*, int, unsigned short*);
int wia_gst_init(void);
#define SETUP() do { if (wia_gst_init()) { printf("ABI: table init failed\n"); return 1; } } while (0)
static void thunk(void){
    /* The first change in this project that WRITES a caller-supplied buffer, so the gate is checking
       that a store loop leaves the non-volatile registers and the stack alone as well as the scans do.
       A frame with seven saved registers and one internal routine (the length scan for cch = -1).

       ALL THE PATHS ARE DRIVEN: all three info types, an invalid info type (which returns before any
       table base is computed), cch positive and cch = -1 (which calls the length scan), a NULL source
       and a NULL destination, a count of zero, counts that are and are not multiples of the unroll, and
       strings of ASCII, Latin-1, CJK and surrogates so the table is read at both ends.

       Armed PER CALL (CALL4). */
    static wchar_t s[600];
    static unsigned short out[700];
    unsigned long long sink = 0;
    int i, k;

    for (i = 0; i < 599; ++i) s[i] = (wchar_t)(L'a' + (i % 26));
    s[599] = 0;

    for (k = 0; k < 4; ++k) {
        const wchar_t* p = s + k;
        static const int counts[] = { 1, 2, 3, 7, 8, 9, 16, 17, 64, 511, 595, -1 };
        static const unsigned long kinds[] = { 1, 2, 4, 0, 3, 8 };
        int c, q;
        for (q = 0; q < (int)(sizeof(kinds) / sizeof(kinds[0])); ++q)
            for (c = 0; c < (int)(sizeof(counts) / sizeof(counts[0])); ++c)
                sink += (unsigned long long)CALL4(wia_getstringtypew, kinds[q], p, counts[c], out);
        sink += (unsigned long long)CALL4(wia_getstringtypew, 1, p, 0, out);
        sink += (unsigned long long)CALL4(wia_getstringtypew, 1, 0, 8, out);
        sink += (unsigned long long)CALL4(wia_getstringtypew, 1, p, 8, 0);
    }

    /* the far side of the table, and the surrogate range */
    for (i = 0; i < 511; ++i) s[i] = (wchar_t)(0x4E00 + i);
    s[511] = 0;
    sink += (unsigned long long)CALL4(wia_getstringtypew, 1, s, 511, out);
    sink += (unsigned long long)CALL4(wia_getstringtypew, 4, s, -1, out);
    for (i = 0; i < 511; ++i) s[i] = (wchar_t)(0xD800 + (i % 0x800));
    s[511] = 0;
    sink += (unsigned long long)CALL4(wia_getstringtypew, 1, s, 511, out);
    sink += (unsigned long long)CALL4(wia_getstringtypew, 2, s, 511, out);
    if (sink == 0xFFFFFFFFFFFFFFFFull) printf("");
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
    m |= callmask;   /* whatever the per-call arming saw, which the thunk cannot have hidden */

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
