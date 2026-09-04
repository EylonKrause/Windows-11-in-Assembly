#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } USTR;
typedef LONG NTSTATUS;
typedef NTSTATUS (WINAPI *dc_fn)(USTR*,const USTR*,BOOLEAN);
unsigned short wia_downcase[65536];
void wia_downcase_init(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    dc_fn f=(dc_fn)GetProcAddress(h,"RtlDowncaseUnicodeString");
    for(int i=0;i<65536;i++){
        wchar_t c=(wchar_t)i, o=0; USTR src={2,2,&c}, dst={0,2,&o};
        f(&dst,&src,FALSE);
        wia_downcase[i]=(unsigned short)o;
    }
}
