// changes/071-wcsrev/reference.c — scalar oracle for _wcsrev.
#include <stddef.h>
#include <wchar.h>
wchar_t* ref_wcsrev(wchar_t* s){
    size_t n=0; while(s[n]) n++;
    if(n){ for(size_t i=0,j=n-1;i<j;i++,j--){ wchar_t t=s[i]; s[i]=s[j]; s[j]=t; } }
    return s;
}
