#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } USTR;
typedef struct { unsigned short Length, MaximumLength; char* Buffer; } OSTR;
typedef LONG NTSTATUS;
typedef NTSTATUS (WINAPI *fn)(USTR*,const OSTR*,BOOLEAN);
unsigned short wia_oem2umap[256];
void wia_oem2umap_init(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    fn f=(fn)GetProcAddress(h,"RtlOemStringToUnicodeString");
    for(int i=0;i<256;i++){
        char c=(char)i; wchar_t o[4]={0,0,0,0};
        OSTR src={1,1,&c}; USTR dst={0,8,o};
        f(&dst,&src,FALSE);
        wia_oem2umap[i]=(unsigned short)(dst.Length>=2?o[0]:(unsigned short)i);
    }
}
