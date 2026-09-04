// scalar reference for RtlUTF8ToUnicodeN (validated vs ntdll: 0 mismatches / 400000).
typedef long NTSTATUS;
NTSTATUS ref_u82u(unsigned short* d, unsigned long dbytes, unsigned long* outLen,
                  const unsigned char* s, unsigned long n){
    unsigned long dwmax=dbytes/2, i=0, o=0; int nm=0, ov=0;
    while(i<n){
        unsigned L=s[i];
        if(L<0x80){ if(o+1>dwmax){ov=1;break;} d[o++]=(unsigned short)L; i++; continue; }
        if(L<0xC2 || L>=0xF5){ nm=1; if(o+1>dwmax){ov=1;break;} d[o++]=0xFFFD; i++; continue; }
        unsigned expected=(L<0xE0)?2:(L<0xF0)?3:4, lo=0x80, hi=0xBF;
        if(L==0xE0){lo=0xA0;} else if(L==0xED){hi=0x9F;} else if(L==0xF0){lo=0x90;} else if(L==0xF4){hi=0x8F;}
        unsigned consumed=1; int valid=1;
        for(unsigned j=1;j<expected;j++){
            if(i+j>=n){valid=0;break;}
            unsigned b=s[i+j];
            if(b<0x80||b>0xBF){valid=0;break;}
            consumed++;
            if(j==1 && (b<lo||b>hi)){valid=0;break;}
        }
        if(valid){
            unsigned cp;
            if(expected==2) cp=((L&0x1F)<<6)|(s[i+1]&0x3F);
            else if(expected==3) cp=((L&0xF)<<12)|((s[i+1]&0x3F)<<6)|(s[i+2]&0x3F);
            else cp=((L&7)<<18)|((s[i+1]&0x3F)<<12)|((s[i+2]&0x3F)<<6)|(s[i+3]&0x3F);
            if(cp>0xFFFF){ unsigned h=0xD800+((cp-0x10000)>>10), l=0xDC00+((cp-0x10000)&0x3FF);
                if(o+1>dwmax){ov=1;break;} d[o++]=(unsigned short)h;
                if(o+1>dwmax){ov=1;break;} d[o++]=(unsigned short)l; }
            else { if(o+1>dwmax){ov=1;break;} d[o++]=(unsigned short)cp; }
            i+=expected;
        } else { nm=1; if(o+1>dwmax){ov=1;break;} d[o++]=0xFFFD; i+=consumed; }
    }
    *outLen=o*2;
    if(ov) return (NTSTATUS)0xC0000023;
    if(nm) return (NTSTATUS)0x00000107;
    return 0;
}
