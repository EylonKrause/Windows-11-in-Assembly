// changes/123-rtlfindlongestrunclear/reference.c
// Independent, obviously-correct oracle for ntdll!RtlFindLongestRunClear: the earliest longest run of
// clear (0) bits in the first SizeOfBitMap bits. Validated bit-exact vs the live export (length +
// *StartingIndex) over edge cases + 3M fuzz before the asm. bit i -> Buffer[i/32] bit (i%32), LSB low.
typedef struct { unsigned long SizeOfBitMap; unsigned long* Buffer; } RTL_BITMAP;
unsigned long ref_lrc(const RTL_BITMAP* bm, unsigned long* start){
    unsigned long n=bm->SizeOfBitMap; const unsigned long* b=bm->Buffer;
    unsigned long best=0,bestStart=0,curLen=0,curStart=0;
    for(unsigned long i=0;i<n;i++){
        unsigned long bit=(b[i>>5]>>(i&31))&1u;
        if(!bit){ if(curLen==0)curStart=i; curLen++; if(curLen>best){best=curLen;bestStart=curStart;} }
        else curLen=0;
    }
    *start=bestStart; return best;
}
