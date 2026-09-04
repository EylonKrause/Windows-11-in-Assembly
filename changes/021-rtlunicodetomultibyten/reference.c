typedef long NTSTATUS;
extern unsigned char wia_ansimap[65536];
NTSTATUS ref_u2mb(unsigned char* dst, unsigned long maxBytes, unsigned long* outLen,
                  const unsigned short* src, unsigned long srcBytes){
    unsigned long n=srcBytes/2, i=0;
    for(; i<n && i<maxBytes; i++) dst[i]=wia_ansimap[src[i]];
    *outLen=i;
    return 0;
}
