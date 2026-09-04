typedef long NTSTATUS;
extern unsigned char wia_oemmap[65536];
NTSTATUS ref_u2oemn(unsigned char* dst, unsigned long maxBytes, unsigned long* outLen,
                  const unsigned short* src, unsigned long srcBytes){
    unsigned long n=srcBytes/2, i=0;
    for(; i<n && i<maxBytes; i++) dst[i]=wia_oemmap[src[i]];
    *outLen=i;
    return (i<n)? (NTSTATUS)0x80000005 : 0;
}
