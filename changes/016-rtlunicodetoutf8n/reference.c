typedef long NTSTATUS;
NTSTATUS ref_u2u8(unsigned char* dst, unsigned long dstMax, unsigned long* outLen,
                  const unsigned short* src, unsigned long srcBytes){
    unsigned long n=srcBytes/2, o=0; int nm=0, ov=0;
    for(unsigned long i=0;i<n && !ov;i++){
        unsigned c=src[i];
        if(c<0x80){ if(o+1>dstMax){ov=1;break;} dst[o]=(unsigned char)c; o+=1; }
        else if(c<0x800){ if(o+2>dstMax){ov=1;break;} dst[o]=0xC0|(c>>6);dst[o+1]=0x80|(c&0x3F); o+=2; }
        else if(c>=0xD800 && c<0xDC00){
            if(i+1<n && src[i+1]>=0xDC00 && src[i+1]<0xE000){
                unsigned cp=0x10000+((c-0xD800)<<10)+(src[i+1]-0xDC00);
                if(o+4>dstMax){ov=1;break;} dst[o]=0xF0|(cp>>18);dst[o+1]=0x80|((cp>>12)&0x3F);dst[o+2]=0x80|((cp>>6)&0x3F);dst[o+3]=0x80|(cp&0x3F); o+=4; i++;
            } else { nm=1; if(o+3>dstMax){ov=1;break;} dst[o]=0xEF;dst[o+1]=0xBF;dst[o+2]=0xBD; o+=3; }
        }
        else if(c>=0xDC00 && c<0xE000){ nm=1; if(o+3>dstMax){ov=1;break;} dst[o]=0xEF;dst[o+1]=0xBF;dst[o+2]=0xBD; o+=3; }
        else { if(o+3>dstMax){ov=1;break;} dst[o]=0xE0|(c>>12);dst[o+1]=0x80|((c>>6)&0x3F);dst[o+2]=0x80|(c&0x3F); o+=3; }
    }
    *outLen=o;
    if(ov) return (NTSTATUS)0xC0000023;
    if(nm) return (NTSTATUS)0x00000107;
    return 0;
}
