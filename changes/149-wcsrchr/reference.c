// changes/149-wcsrchr/reference.c
// Oracle for ucrtbase!wcsrchr: last occurrence of c, else NULL. Searching for 0 returns a pointer to
// the terminator (standard C -- and the opposite of shlwapi!StrRChrW, change 134, which returns NULL).
#include <wchar.h>
const wchar_t* ref_wcsrchr(const wchar_t* s, wchar_t c){
    const wchar_t* last=0;
    for(;;){
        if(*s==c) last=s;
        if(*s==0) break;
        ++s;
    }
    return last;
}
