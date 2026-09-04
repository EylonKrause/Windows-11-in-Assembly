unsigned int wia_hex2uw[256];
void wia_hex2uw_init(void){ static const char* h="0123456789ABCDEF";
    for(int i=0;i<256;i++) wia_hex2uw[i]=(unsigned)(unsigned char)h[i>>4] | ((unsigned)(unsigned char)h[i&15]<<16); }
