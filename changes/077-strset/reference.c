// changes/077-strset/reference.c — scalar oracle for _strset.
char* ref_strset(char* s, int c){ char* p=s; while(*p) *p++=(char)c; return s; }
