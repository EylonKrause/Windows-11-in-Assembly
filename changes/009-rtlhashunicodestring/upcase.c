#define WIN32_LEAN_AND_MEAN
#include <windows.h>
unsigned short wia_upcase[65536];
typedef WCHAR (WINAPI *upc_fn)(WCHAR);
void wia_upcase_init(void){ HMODULE h=LoadLibraryW(L"ntdll.dll"); upc_fn u=(upc_fn)GetProcAddress(h,"RtlUpcaseUnicodeChar"); for(int i=0;i<65536;i++) wia_upcase[i]=(unsigned short)u((WCHAR)i); }
