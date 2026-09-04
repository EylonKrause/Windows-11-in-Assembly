#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } USTR;
typedef struct { unsigned short Length, MaximumLength; char* Buffer; } ASTR;
typedef LONG NTSTATUS;
typedef NTSTATUS (WINAPI *fn)(ASTR*,const USTR*,BOOLEAN);
unsigned char wia_ansimap[65536];
void wia_ansimap_init(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    fn f=(fn)GetProcAddress(h,"RtlUnicodeStringToAnsiString");
    for(int i=0;i<65536;i++){
        wchar_t c=(wchar_t)i; char o[4]={0,0,0,0};
        USTR src={2,2,&c}; ASTR dst={0,4,o};
        f(&dst,&src,FALSE);
        wia_ansimap[i]=(unsigned char)(dst.Length>=1?o[0]:0x3F);
    }
}
