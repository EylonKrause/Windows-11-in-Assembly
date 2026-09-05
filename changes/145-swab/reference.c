// changes/145-swab/reference.c
// Oracle for ucrtbase!_swab: copy n bytes swapping adjacent pairs, strictly forward (so an overlapping
// dest > src re-reads bytes it already wrote -- reproduced here deliberately). Odd n leaves the last
// destination byte untouched. Negative n is treated as a no-op (the live routine overruns; see impl).
void ref_swab(char* src, char* dest, int n){
    if(n<2) return;
    int pairs=n/2;
    for(int i=0;i<pairs;i++){
        char a=src[2*i], b=src[2*i+1];
        dest[2*i]=b; dest[2*i+1]=a;
    }
}
