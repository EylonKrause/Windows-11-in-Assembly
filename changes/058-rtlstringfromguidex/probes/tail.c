/* probes/tail.c: where does RtlStringFromGUIDEx put its extra NUL, and does it move?
 *
 * The live harness found the shipped export writing a second terminator past the one after the
 * 38-character GUID string, on 13333 of 20000 cases, with the status and Length matching. The
 * question the IPv4/IPv6 pair taught to ask is whether that index is FIXED or follows something --
 * MaximumLength, say, so this varies MaximumLength and prints every zero position.
 *
 * It matters that the capacities below include ODD values and values well past the 38-character
 * string. An earlier, narrower version of this probe asked only about 78, 79, 80, 82, 90, 100,
 * 120 and 160, and correctness.c concluded from it that the extra NULs were "an internal artifact
 * with no clean rule". They follow a perfectly clean rule; the probe was just too narrow to see
 * that it is integer division, and at the two smallest capacities the second terminator lands on
 * top of the first. A rule a probe cannot see is not the absence of a rule.
 */
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef LONG NTSTATUS_;
typedef struct { USHORT Length, MaximumLength; WCHAR* Buffer; } USTR;
typedef NTSTATUS_ (NTAPI *fnG)(const GUID*, USTR*, BOOLEAN);

#define P 0x71

int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    fnG sg=(fnG)GetProcAddress(h,"RtlStringFromGUIDEx");
    static const USHORT MAXL[] = { 78, 79, 80, 81, 82, 83, 90, 91,
                                   100, 101, 120, 121, 160, 161, 200, 398 };
    GUID g; int i,k;
    if(!sg){ printf("cannot resolve\n"); return 2; }
    for(k=0;k<16;++k) ((unsigned char*)&g)[k]=(unsigned char)(k*17);

    printf("%-10s %-8s %-8s %-22s %s\n","MaxLength","status","Length","zero WCHAR positions","ML/2-1");
    printf("---------------------------------------------------------------------------\n");
    for(i=0;i<16;++i){
        unsigned char buf[800]; USTR u; NTSTATUS_ r; int z; char z_str[128]; int n=0;
        memset(buf,P,sizeof buf);
        u.Length=0; u.MaximumLength=MAXL[i]; u.Buffer=(WCHAR*)buf;
        r = sg(&g,&u,FALSE);
        z_str[0]=0;
        for(z=0; z<400; ++z)
            if(((WCHAR*)buf)[z]==0) n+=sprintf(z_str+n," %d", z);
        printf("%-10u %08lX %-8u %-22s %d\n",
               MAXL[i], (unsigned long)r, u.Length, z_str, MAXL[i]/2 - 1);
    }
    printf("\nThe string is 38 wchars, so a terminator at 38 is the documented one.\n"
           "An index that stays at 39 whatever MaximumLength says is an end-of-field\n"
           "terminator; one that tracks MaximumLength/2-1 is a fill-to-capacity.\n");
    printf("\nMEASURED on the i9-11900H bench, ntdll 10.0.26200: the second index is\n"
           "MaximumLength/2 - 1, integer division, on all sixteen capacities --\n"
           "78->38 79->38 80->39 81->39 82->40 83->40 90->44 91->44 100->49 101->49\n"
           "120->59 121->59 160->79 161->79 200->99 398->198. At 78 and 79 it lands on\n"
           "38, the string's own terminator, so a probe that stops there sees no rule.\n"
           "impl.asm and reference.c both write it; correctness.c now compares the whole\n"
           "destination buffer rather than excusing everything past the string.\n");
    return 0;
}
