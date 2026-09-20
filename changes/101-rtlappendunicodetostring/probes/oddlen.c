/* appus2.c -- sweep the shape the live harness actually drives, and find the divergence.
 *
 * appus.c found nothing with a roomy destination, so the 1201 differing cases must come from the
 * harness's own parameters: MaximumLength 256 with a 512-byte buffer, Length anywhere from 0 to
 * 254, and a source of up to 599 characters -- so most appends overflow and some do not.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef LONG NTSTATUS_;
typedef struct { USHORT Length, MaximumLength; WCHAR* Buffer; } USTR;
extern NTSTATUS_ wia_appendus(USTR*, const wchar_t*);
typedef NTSTATUS_ (NTAPI *fn)(USTR*, const wchar_t*);

#define BUFB   512          /* the real buffer, as the harness allocates it */
#define MAXLEN 256          /* what MaximumLength claims */
#define POISON 0x9A

static unsigned char ba[BUFB], bb[BUFB];
static WCHAR src[800];

int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    fn sys=(fn)GetProcAddress(h,"RtlAppendUnicodeToString");
    int dl, sn, shown=0, total=0, diff=0;
    if(!sys){ printf("cannot resolve\n"); return 2; }

    for(sn=0;sn<800;++sn) src[sn]=(WCHAR)(L'a'+(sn%26));

    printf("sweeping Length 0..255 INCLUDING ODD x source 0..600 characters, MaximumLength=%d\n\n", MAXLEN);
    for(dl=0; dl<=255; dl+=1){
        for(sn=0; sn<=600; ++sn){
            USTR da, db; NTSTATUS_ sa, sb; int at=-1, k;
            WCHAR save = src[sn]; src[sn]=0;
            memset(ba,POISON,BUFB); memset(bb,POISON,BUFB);
            da.Length=(USHORT)dl; da.MaximumLength=MAXLEN; da.Buffer=(WCHAR*)ba;
            db.Length=(USHORT)dl; db.MaximumLength=MAXLEN; db.Buffer=(WCHAR*)bb;
            sa = sys(&da, src);
            sb = wia_appendus(&db, src);
            src[sn]=save;
            ++total;
            for(k=0;k<BUFB;++k) if(ba[k]!=bb[k]){ at=k; break; }
            if(at>=0 || sa!=sb || da.Length!=db.Length){
                ++diff;
                if(shown<6){
                    printf("dLen=%-4d srcN=%-4d  live st=%08lX len=%-4u | ours st=%08lX len=%-4u | "
                           "first diff byte %d: live %02X ours %02X\n",
                           dl, sn, (unsigned long)sa, da.Length, (unsigned long)sb, db.Length,
                           at, at<0?0:ba[at], at<0?0:bb[at]);
                    if(at>=0){
                        int lo = at>6 ? at-6 : 0;
                        printf("      live @%d:",lo); for(k=lo;k<lo+14 && k<BUFB;++k) printf(" %02X",ba[k]);
                        printf("\n      ours @%d:",lo); for(k=lo;k<lo+14 && k<BUFB;++k) printf(" %02X",bb[k]);
                        printf("\n");
                    }
                    ++shown;
                }
            }
        }
    }
    printf("\n%d of %d cases differ\n", diff, total);
    return 0;
}
