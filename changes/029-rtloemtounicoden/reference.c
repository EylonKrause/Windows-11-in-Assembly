typedef long NTSTATUS;
extern unsigned short wia_oem2umap[256];
NTSTATUS ref_oem2un(unsigned short* dst, unsigned long maxBytes, unsigned long* outLen,
                  const unsigned char* src, unsigned long srcBytes){
    unsigned long n=srcBytes, maxW=maxBytes/2, i=0;
    for(; i<n && i<maxW; i++) dst[i]=wia_oem2umap[src[i]];
    *outLen=i*2;
    return (i<n)? (NTSTATUS)0x80000005 : 0;
}
