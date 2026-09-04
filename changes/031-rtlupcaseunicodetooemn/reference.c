typedef long NTSTATUS;
extern unsigned char wia_upoemmap[65536];
NTSTATUS ref_u2uoem(unsigned char* dst, unsigned long maxBytes, unsigned long* outLen,
                  const unsigned short* src, unsigned long srcBytes){
    unsigned long n=srcBytes/2, i=0;
    for(; i<n && i<maxBytes; i++) dst[i]=wia_upoemmap[src[i]];
    *outLen=i;
    return (i<n)? (NTSTATUS)0x80000005 : 0;
}
