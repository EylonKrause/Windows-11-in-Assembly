#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern int wia_istextunicode(const void*, int, int*);
int ref_istextunicode(const void*, int, int*);
static unsigned char b[4096];
int main(void){
    setvbuf(stdout,0,_IONBF,0);
    for(int i=0;i<4096;i++) b[i]=0x61;
    for(int len=0; len<=600; ++len){
        printf("len=%d ... ", len); fflush(stdout);
        int f=-1;
        int r = wia_istextunicode(b,len,&f);
        int g=-1;
        int q = ref_istextunicode(b,len,&g);
        printf("ours=%d/%04X ref=%d/%04X %s\n", r,f,q,g, (r==q&&f==g)?"":"  <== DIFF");
    }
    printf("done\n");
    return 0;
}
