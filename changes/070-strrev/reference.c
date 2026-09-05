// changes/070-strrev/reference.c — scalar oracle for _strrev.
#include <stddef.h>
char* ref_strrev(char* s){
    size_t n=0; while(s[n]) n++;
    if(n){ for(size_t i=0,j=n-1;i<j;i++,j--){ char t=s[i]; s[i]=s[j]; s[j]=t; } }
    return s;
}
