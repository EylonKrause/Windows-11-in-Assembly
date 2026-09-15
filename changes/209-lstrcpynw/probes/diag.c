#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern wchar_t* wia_lstrcpynw(wchar_t*, const wchar_t*, int);
wchar_t* ref_lstrcpynw(wchar_t*, const wchar_t*, int);
typedef wchar_t* (WINAPI *FN)(wchar_t*, const wchar_t*, int);
#define PW ((wchar_t)0x2A2A)
#define DSZ 128
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
    static wchar_t src[96], a[DSZ], b[DSZ], c[DSZ];
    int shown = 0;
    for (int sl = 0; sl <= 80 && shown < 14; ++sl) {
        for (int i = 0; i < sl; ++i) src[i] = (wchar_t)(L'a' + (i % 26));
        src[sl] = 0;
        for (int n = 0; n <= 84 && shown < 14; ++n) {
            for (int i = 0; i < DSZ; ++i) { a[i]=PW; b[i]=PW; c[i]=PW; }
            wchar_t* ra = wia_lstrcpynw(a, src, n);
            wchar_t* rb = ref_lstrcpynw(b, src, n);
            wchar_t* rc = sys(c, src, n);
            int bad = ((ra==a) != (rc==c)) || ((rb==b) != (rc==c));
            if (!bad) for (int i = 0; i < DSZ; ++i) if (a[i]!=c[i] || b[i]!=c[i]) { bad = 1; break; }
            if (bad) {
                printf("srclen=%-3d n=%-3d\n", sl, n);
                printf("   ours [" ); dump(a, 24); printf("]  ret=%s\n", ra==a?"dst":(ra?"?":"NULL"));
                printf("   ref  [" ); dump(b, 24); printf("]  ret=%s\n", rb==b?"dst":(rb?"?":"NULL"));
                printf("   live [" ); dump(c, 24); printf("]  ret=%s\n", rc==c?"dst":(rc?"?":"NULL"));
                ++shown;
            }
        }
    }
    if (!shown) printf("no mismatch in the swept region\n");
    return 0;
}
