/* ncat0.c -- what does strncat_s really do when count == 0 and src == NULL?
 *
 * changes/156's header states rule 3 as: "count == 0 AND src == NULL -> return 0, NOTHING WRITTEN
 * and no handler". The live harness found three cases where the shipped export disagrees --
 * size = 1 with a non-empty destination -- returning EINVAL, emptying the destination and calling
 * the invalid-parameter handler.
 *
 * So the question is the PRECEDENCE: does the destination-terminator check come before that
 * early-out, or does the early-out require a valid destination? This asks the export directly over
 * the whole small grid: destination contents x size x (src NULL or not) x count 0 or not.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static volatile long iph_calls;
static void __cdecl iph(const wchar_t* a,const wchar_t* b,const wchar_t* c,unsigned d,uintptr_t e){
    (void)a;(void)b;(void)c;(void)d;(void)e; ++iph_calls;
}

typedef int (__cdecl *fn)(char*, size_t, const char*, size_t);

int main(void){
    HMODULE h; fn sys;
    static const char* DST[] = { "", "A", "AB", "ABCD" };
    static const size_t SZ[] = { 0, 1, 2, 3, 5 };
    int di, si, nullsrc, cz;
    _set_invalid_parameter_handler(iph);
    h=LoadLibraryW(L"ucrtbase.dll");
    sys=(fn)GetProcAddress(h,"strncat_s");
    if(!sys){ printf("cannot resolve strncat_s\n"); return 2; }

    printf("%-6s %-5s %-8s %-6s  %-4s %-4s %-18s\n",
           "dst","size","src","count","rc","iph","destination after");
    printf("---------------------------------------------------------------------\n");
    for(di=0; di<4; ++di)
    for(si=0; si<5; ++si)
    for(nullsrc=0; nullsrc<2; ++nullsrc)
    for(cz=0; cz<2; ++cz){
        char buf[16]; int rc; long before; int k;
        memset(buf,0x7E,sizeof buf);
        memcpy(buf, DST[di], strlen(DST[di])+1);
        before = iph_calls;
        rc = sys(buf, SZ[si], nullsrc?NULL:"xy", cz?0:1);
        printf("%-6s %-5llu %-8s %-6d  %-4d %-4ld ", DST[di][0]?DST[di]:"(empty)",
               (unsigned long long)SZ[si], nullsrc?"NULL":"\"xy\"", cz?0:1,
               rc, iph_calls-before);
        for(k=0;k<6;++k) printf("%02X ", (unsigned char)buf[k]);
        printf("\n");
    }
    printf("\nrc 22 = EINVAL, 34 = ERANGE, 80 = STRUNCATE, 0 = success.\n");
    return 0;
}
