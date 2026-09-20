// changes/058-rtlstringfromguidex/reference.c — scalar oracle for RtlStringFromGUIDEx (Allocate=FALSE).
typedef long NTSTATUS;
typedef struct { unsigned long Data1; unsigned short Data2, Data3; unsigned char Data4[8]; } GUIDX;
typedef struct { unsigned short Length, MaximumLength; unsigned short* Buffer; } U;
NTSTATUS ref_guidfmt(const GUIDX* g, U* s){
    static const char* h="0123456789abcdef";
    if(s->MaximumLength < 78) return (NTSTATUS)0xC0000023;
    const unsigned char* b=(const unsigned char*)g;
    static const int ord[16]={3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15};
    unsigned short* p=s->Buffer; *p++='{';
    for(int i=0;i<16;i++){ unsigned char v=b[ord[i]]; *p++=h[v>>4]; *p++=h[v&15];
        if(i==3||i==5||i==7||i==9) *p++='-'; }
    *p++='}'; *p=0;
    // The export terminates TWICE: after the 38 characters, and again at the last whole WCHAR the
    // caller's capacity allows. Measured over 16 capacities, odd and even (probes/tail.c):
    // 78->38 79->38 80->39 81->39 82->40 83->40 90->44 100->49 120->59 160->79 200->99 398->198,
    // i.e. MaximumLength/2 - 1 with integer division, at 78 and 79 it coincides with the first
    // terminator, which is why a narrow probe reads as "no rule". MaximumLength >= 78 is already
    // guaranteed above, so the index is never negative.
    s->Buffer[s->MaximumLength/2 - 1] = 0;
    s->Length=76;
    return 0;
}
