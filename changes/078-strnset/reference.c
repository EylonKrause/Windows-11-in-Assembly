// changes/078-strnset/reference.c — scalar oracle for _strnset.
#include <stddef.h>
char* ref_strnset(char* s, int c, size_t n){ char* p=s; while(n && *p){ *p++=(char)c; n--; } return s; }
