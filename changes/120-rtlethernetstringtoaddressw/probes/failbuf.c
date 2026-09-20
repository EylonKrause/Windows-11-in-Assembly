/* ethfail.c -- on a FAILED parse, what do these leave in the caller's address buffer?
 *
 * The live harness reports the status and the terminator matching on every sampled case while the
 * address buffer differs -- 10678 of 20000 for RtlEthernetStringToAddress and ~6900 for the IPv6
 * pair. So the question is what each one writes before giving up.
 */
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef LONG NTSTATUS_;
extern NTSTATUS_ wia_ethstra(const char*, const char**, unsigned char*);
extern NTSTATUS_ wia_ip6a   (const char*, const char**, unsigned char*);
typedef NTSTATUS_ (NTAPI *fnE)(const char*, const char**, void*);
typedef NTSTATUS_ (NTAPI *fnI)(const char*, const char**, void*);

#define P 0xC4

static void show(const char* tag, const unsigned char* b, int n){
    int k; printf("    %-6s", tag);
    for(k=0;k<n;++k) printf(" %02X", b[k]);
    printf("\n");
}

int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll");
    fnE se=(fnE)GetProcAddress(h,"RtlEthernetStringToAddressA");
    fnI si=(fnI)GetProcAddress(h,"RtlIpv6StringToAddressA");
    static const char* S[] = {
        "182.77.169.58", "4c-24-f2-2a-59", "zzz", "", "00-11-22-33-44-55",
        "00-11-22-33-44", "00-11-22-33-44-55-66", "00:11:22:33:44:55", "0"
    };
    int i,k;
    if(!se||!si){ printf("cannot resolve\n"); return 2; }

    printf("RtlEthernetStringToAddressA -- 6-byte buffer poisoned with C4\n");
    for(i=0;i<9;++i){
        unsigned char a[8], b[8]; const char* ta=NULL; const char* tb=NULL;
        NTSTATUS_ ra, rb;
        memset(a,P,sizeof a); memset(b,P,sizeof b);
        ra = se(S[i], &ta, a);
        rb = wia_ethstra(S[i], &tb, b);
        printf("  \"%s\"  live st=%08lX t=%d | ours st=%08lX t=%d%s\n", S[i],
               (unsigned long)ra, ta?(int)(ta-S[i]):-1,
               (unsigned long)rb, tb?(int)(tb-S[i]):-1,
               memcmp(a,b,8)?"   BUFFERS DIFFER":"");
        if(memcmp(a,b,8)){ show("live",a,8); show("ours",b,8); }
    }

    printf("\nRtlIpv6StringToAddressA -- 16-byte buffer poisoned with C4\n");
    for(i=0;i<9;++i){
        unsigned char a[16], b[16]; const char* ta=NULL; const char* tb=NULL;
        NTSTATUS_ ra, rb;
        memset(a,P,sizeof a); memset(b,P,sizeof b);
        ra = si(S[i], &ta, a);
        rb = wia_ip6a(S[i], &tb, b);
        printf("  \"%s\"  live st=%08lX t=%d | ours st=%08lX t=%d%s\n", S[i],
               (unsigned long)ra, ta?(int)(ta-S[i]):-1,
               (unsigned long)rb, tb?(int)(tb-S[i]):-1,
               memcmp(a,b,16)?"   BUFFERS DIFFER":"");
        if(memcmp(a,b,16)){ show("live",a,16); show("ours",b,16); }
    }
    return 0;
}
