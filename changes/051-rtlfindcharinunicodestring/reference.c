// changes/051-rtlfindcharinunicodestring/reference.c — scalar oracle for RtlFindCharInUnicodeString.
typedef unsigned short u16;
typedef struct { unsigned short Length, MaximumLength; u16* Buffer; } U;
extern u16 wia_upcase[];
static int inset(u16 c, const U* set, int ci){
    u16 cc = ci ? wia_upcase[c] : c;
    int n = set->Length/2;
    for(int i=0;i<n;i++){ u16 s=set->Buffer[i]; if(ci) s=wia_upcase[s]; if(s==cc) return 1; }
    return 0;
}
long ref_findchar(unsigned long flags, const U* str, const U* set, u16* pos){
    int ci=(flags&4)!=0, compl=(flags&2)!=0, atend=(flags&1)!=0;
    int n=str->Length/2, i;
    *pos=0;
    if(atend){
        for(i=n-1;i>=0;i--){ int m=inset(str->Buffer[i],set,ci); if(m^compl){ *pos=(u16)(i*2); return 0; } }
    } else {
        for(i=0;i<n;i++){ int m=inset(str->Buffer[i],set,ci); if(m^compl){ *pos=(u16)((i+1)*2); return 0; } }
    }
    return (long)0xC0000225;
}
