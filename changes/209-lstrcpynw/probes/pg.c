#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern wchar_t* wia_lstrcpynw(wchar_t*, const wchar_t*, int);
typedef wchar_t* (WINAPI *FN)(wchar_t*, const wchar_t*, int);
#define PW ((wchar_t)0x2A2A)
#define DSZ 160
static void dump(const wchar_t* d, int n){
    for (int i = 0; i < n; ++i) {
        wchar_t c = d[i];
        putchar(c == 0 ? '.' : (c == PW ? '-' : (c < 128 ? (char)c : '?')));
    }
}
int main(void){
    setvbuf(stdout,0,_IONBF,0);
    HMODULE h = LoadLibraryW(L"kernelbase.dll");
    FN sys = (FN)GetProcAddress(h, "lstrcpynW");
    SYSTEM_INFO si; GetSystemInfo(&si);
    SIZE_T pg = si.dwPageSize;
    char* base = (char*)VirtualAlloc(NULL, pg*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    DWORD old; VirtualProtect(base + pg, pg, PAGE_NOACCESS, &old);
    static wchar_t da[DSZ], dc[DSZ];
    int shown = 0;
    printf("unterminated source of length sl ending at the guard page; bound n\n\n");
    for (int sl = 1; sl <= 80 && shown < 12; ++sl) {
        wchar_t* s = (wchar_t*)((base + pg) - (SIZE_T)sl * sizeof(wchar_t));
        for (int i = 0; i < sl; ++i) s[i] = (wchar_t)(L'a' + (i % 26));
        for (int n = 1; n <= 120 && shown < 12; n += 7) {
            for (int i = 0; i < DSZ; ++i) { da[i]=PW; dc[i]=PW; }
            wchar_t* ra = wia_lstrcpynw(da, s, n);
            wchar_t* rc = sys(dc, s, n);
            int ok = ((ra==da) == (rc==dc));
            if (ok) for (int i = 0; i < DSZ; ++i) if (da[i]!=dc[i]) { ok = 0; break; }
            if (!ok) {
                printf("sl=%-3d n=%-4d  (src page offset of s = %u)\n",
                       sl, n, (unsigned)((uintptr_t)s & 4095));
                printf("   ours [" ); dump(da, 40); printf("]  ret=%s\n", ra==da?"dst":(ra?"?":"NULL"));
                printf("   live [" ); dump(dc, 40); printf("]  ret=%s\n", rc==dc?"dst":(rc?"?":"NULL"));
                ++shown;
            }
        }
    }
    if (!shown) printf("no page-guard mismatch\n");
    return 0;
}
