// changes/186-wtoi/bench.c
// Gate 2: time wia_wtoi against the live ucrtbase!_wtoi across input shapes.
// The classes cover the frequency split the implementation is built around: ASCII digits (the
// common case), a leading whitespace run, the saturating path, and the two non-ASCII digit
// routes, fullwidth (scalar) and a block that only the AVX2 classifier can resolve.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "bench.h"

extern int wia_wtoi(const wchar_t*);
typedef int (__cdecl *WTOI)(const wchar_t*);
static WTOI sys;

#pragma optimize("", off)
static uint64_t op_ours(void* c){ return (uint64_t)(unsigned)wia_wtoi((const wchar_t*)c); }
static uint64_t op_sys (void* c){ return (uint64_t)(unsigned)sys((const wchar_t*)c); }
#pragma optimize("", on)

static wchar_t fw[32], arab[32], mixed[40], longz[64];

int main(void){
    HMODULE hu = LoadLibraryW(L"ucrtbase.dll");
    sys = (WTOI)GetProcAddress(hu,"_wtoi");

    /* U+FF10.. fullwidth "1234567890", the scalar non-ASCII route */
    { const wchar_t* d=L"1234567890"; int k=0; for(;d[k];k++) fw[k]=(wchar_t)(0xFF10+(d[k]-L'0')); fw[k]=0; }
    /* U+0660.. Arabic-Indic "1234567890", only the AVX2 classifier resolves this block */
    { const wchar_t* d=L"1234567890"; int k=0; for(;d[k];k++) arab[k]=(wchar_t)(0x0660+(d[k]-L'0')); arab[k]=0; }
    /* one digit from each of several different blocks, concatenated */
    { static const unsigned short B[10]={0x0030,0xFF10,0x0660,0x06F0,0x0966,0x09E6,0x0A66,0x0B66,0x0E50,0x1810};
      for(int i=0;i<10;i++) mixed[i]=(wchar_t)(B[i]+((i*3+1)%10)); mixed[10]=0; }
    /* 32 leading zeros then a value, the long-run case change 109 also benches */
    { int k=0; for(;k<32;k++) longz[k]=L'0'; longz[k++]=L'4'; longz[k++]=L'2'; longz[k]=0; }

    enum { N = 8 };
    static wia_case cs[N];
    static const char* names[] = {
        "\"42\"", "\"-1234567890\"", "ws+\"+2147483647\"", "\"99999999999999999999\"",
        "32 zeros + \"42\"", "fullwidth U+FF10..", "Arabic-Indic U+0660..", "10 blocks mixed" };
    static const wchar_t* in[N];
    in[0]=L"42";
    in[1]=L"-1234567890";
    in[2]=L"   \t\n\v\f\r+2147483647";
    in[3]=L"99999999999999999999";
    in[4]=longz;
    in[5]=fw;
    in[6]=arab;
    in[7]=mixed;
    static const size_t bytes[] = { 2,11,20,20,34,10,10,10 };
    for(int i=0;i<N;i++){ cs[i].label=names[i]; cs[i].bytes=bytes[i];
        cs[i].ours=op_ours; cs[i].system=op_sys; cs[i].ctx=(void*)in[i]; }
    return wia_bench_compare("ucrtbase _wtoi / _wtol (wia frameless scalar + AVX2 block classifier vs ucrtbase)", cs, N, 300);
}
