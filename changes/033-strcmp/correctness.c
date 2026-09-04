#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
extern int wia_strcmp(const char*, const char*);
int ref_strcmp(const char*, const char*);
typedef int (__cdecl *fn)(const char*, const char*);
static int failures=0; static int sgn(int x){return (x>0)-(x<0);}
int main(void){
    HMODULE h=LoadLibraryW(L"ucrtbase.dll"); fn sys=(fn)GetProcAddress(h,"strcmp");
    static char a[600],b[600]; unsigned long seed=0x13u;
    for(size_t len=0;len<=280;++len) for(int off=0;off<8;++off){
        char* s1=a+off,*s2=b+off;
        for(size_t i=0;i<len;i++){seed=seed*1103515245u+12345u; char c=(char)((seed>>16)|1); s1[i]=s2[i]=(c?c:3);}
        s1[len]=s2[len]=0;
        if(sgn(ref_strcmp(s1,s2))!=sgn(wia_strcmp(s1,s2))||sgn(sys(s1,s2))!=sgn(wia_strcmp(s1,s2))){printf("FAIL eq len=%zu\n",len);if(++failures>8)return 1;}
        for(size_t pos=0;pos<len;pos+=(len>40?5:1)){ char sv=s2[pos];
            s2[pos]=(char)(sv+1?sv+1:5); if(sgn(ref_strcmp(s1,s2))!=sgn(wia_strcmp(s1,s2))||sgn(sys(s1,s2))!=sgn(wia_strcmp(s1,s2))){printf("FAIL gt len=%zu pos=%zu\n",len,pos);if(++failures>8)return 1;}
            s2[pos]=(char)(sv-1?sv-1:5); if(sgn(ref_strcmp(s1,s2))!=sgn(wia_strcmp(s1,s2))||sgn(sys(s1,s2))!=sgn(wia_strcmp(s1,s2))){printf("FAIL lt len=%zu pos=%zu\n",len,pos);if(++failures>8)return 1;}
            s2[pos]=sv; }
        if(len>0){ char sv=s2[len-1]; s2[len-1]=0; if(sgn(ref_strcmp(s1,s2))!=sgn(wia_strcmp(s1,s2))||sgn(sys(s1,s2))!=sgn(wia_strcmp(s1,s2))){printf("FAIL prefix len=%zu\n",len);if(++failures>8)return 1;} s2[len-1]=sv; }
    }
    if(!failures) printf("CORRECTNESS: PASS (strcmp sign eq/gt/lt/prefix 0..280 x8, vs ucrtbase)\n");
    else printf("CORRECTNESS: FAIL (%d)\n",failures);
    return failures?1:0;
}
