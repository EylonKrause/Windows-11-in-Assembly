#define WIN32_LEAN_AND_MEAN
#include <windows.h>
unsigned char wia_upcase_ansi[256];
typedef CHAR (WINAPI *upr_fn)(CHAR);
void wia_upcase_ansi_init(void){ HMODULE h=LoadLibraryW(L"ntdll.dll"); upr_fn u=(upr_fn)GetProcAddress(h,"RtlUpperChar"); for(int i=0;i<256;i++) wia_upcase_ansi[i]=(unsigned char)u((CHAR)i); }
