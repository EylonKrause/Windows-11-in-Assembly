// changes/076-rtlcrc64/reference.c — scalar oracle for ntdll!RtlCrc64.
// Reverse-engineered: reflected CRC-64, poly 0x9A6C9329AC4BC9B5, internal init = ~Init,
// output = ~crc (i.e. struct[+0x20]=0xFFFF...FF used as both init-xor and xorout).
#include <stdint.h>
#include <stddef.h>
static uint64_t T0[256]; static int inited=0;
static void ini(void){ uint64_t P=0x9A6C9329AC4BC9B5ULL;
    for(int i=0;i<256;i++){ uint64_t c=(uint64_t)i; for(int j=0;j<8;j++) c=(c&1)?(c>>1)^P:(c>>1); T0[i]=c; } inited=1; }
uint64_t ref_crc64(const void* d, size_t n, uint64_t init){
    if(!inited) ini();
    const unsigned char* p=(const unsigned char*)d; uint64_t crc=~init;
    for(size_t i=0;i<n;i++) crc=(crc>>8)^T0[(crc^p[i])&0xFF];
    return ~crc;
}
