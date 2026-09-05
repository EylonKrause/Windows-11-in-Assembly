// changes/118-rtlguidfromstring/bench.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"
typedef struct { unsigned short Length, MaximumLength; wchar_t* Buffer; } WIA_USTR;
extern long wia_guidfromstring(const WIA_USTR*, GUID*);
typedef long (WINAPI *fn)(WIA_USTR*, GUID*);
static fn sys;
#pragma optimize("", off)
static uint64_t op_ours(void*c){ GUID g; wia_guidfromstring((WIA_USTR*)c,&g); return g.Data1; }
static uint64_t op_sys (void*c){ GUID g; sys((WIA_USTR*)c,&g); return g.Data1; }
#pragma optimize("", on)
int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlGUIDFromString");
    static wchar_t s[]=L"{12345678-9abc-def0-1234-56789abcdef0}";
    static WIA_USTR u; u.Buffer=s; u.Length=76; u.MaximumLength=78;
    static wia_case cs[1];
    cs[0].label="guid38"; cs[0].bytes=38; cs[0].ours=op_ours; cs[0].system=op_sys; cs[0].ctx=&u;
    return wia_bench_compare("ntdll RtlGUIDFromString (wia scalar vs ntdll)", cs, 1, 400);
}
