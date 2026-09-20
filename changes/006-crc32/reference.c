// changes/006-crc32/reference.c: oracle: standard CRC-32 (matches ntdll!RtlComputeCrc32).
#include <stdint.h>
#include <stddef.h>
static uint32_t T[256]; static int T_ready;
static void crc_table_init(void){ for(uint32_t i=0;i<256;i++){uint32_t c=i;for(int k=0;k<8;k++)c=(c&1)?(0xEDB88320u^(c>>1)):(c>>1);T[i]=c;} T_ready=1; }
uint32_t ref_crc32(uint32_t init,const void*p,int len){
    if(!T_ready) crc_table_init();
    const unsigned char* s=(const unsigned char*)p; uint32_t c=~init;
    for(int i=0;i<len;i++) c=(c>>8)^T[(c^s[i])&0xFF];
    return ~c;
}
