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
