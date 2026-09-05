// changes/147-strtok-s/reference.c
// Oracle for ucrtbase!strtok_s (standard C semantics, verified against the live export).
static int wia_in(char c, const char* set){
    for(; *set; ++set) if(*set==c) return 1;
    return 0;
}
char* ref_strtok_s(char* str, const char* delim, char** ctx){
    if(str==0) str=*ctx;
    while(*str && wia_in(*str,delim)) str++;      // skip leading delimiters (left intact)
    if(*str==0){ *ctx=str; return 0; }
    char* tok=str;
    while(*str && !wia_in(*str,delim)) str++;
    if(*str){ *str=0; str++; }
    *ctx=str;
    return tok;
}
