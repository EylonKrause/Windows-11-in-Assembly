typedef long NTSTATUS;
extern unsigned short wia_a2umap[256];
NTSTATUS ref_mb2u(unsigned short* dst, unsigned long maxBytes, unsigned long* outLen,
                  const unsigned char* src, unsigned long srcBytes){
    unsigned long n=srcBytes, maxW=maxBytes/2, i=0;
    for(; i<n && i<maxW; i++) dst[i]=wia_a2umap[src[i]];
    *outLen=i*2;
    return 0;
}
