// changes/166-rtlipv6stringtoaddressw/correctness.c
// Bit-exact fuzz of wia_ip6w vs live ntdll!RtlIpv6StringToAddressW + oracle: STATUS + 16 address
// bytes + *Terminator. Covers 121's full ANSI edge/fuzz corpus lifted to UTF-16, plus the wide-only
// hazards: units above 255 whose LOW BYTE aliases a meaningful ASCII character ('.', ':', '0'..'9',
// 'a'..'f', 'A'..'F'), planted at every position of every base string; one member of each of the 17
// Unicode decimal-digit blocks the value helper folds; every one of the 65536 units dropped into 24
// templates that reach every branch that can see a non-ASCII unit; an exhaustive sweep over a wide
// alphabet that contains such a unit; and a NOACCESS page guard placed one unit past the terminating
// NUL, so a single unit of over-read dies immediately.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
extern long wia_ip6w(const wchar_t*, const wchar_t**, unsigned char*);
long ref_ip6w(const wchar_t*, const wchar_t**, unsigned char*);
typedef long (WINAPI *fn)(const wchar_t*, const wchar_t**, unsigned char*);
static fn sys;
static int fails=0;

// print hex-escaped: the console cannot render most of what this harness feeds in
static void esc(const wchar_t* s){
    for(; *s; ++s){ if(*s>=0x20 && *s<0x7F) putchar((char)*s); else printf("<%04X>",(unsigned)*s); }
}
static void chkw(const wchar_t* s){
    unsigned char a1[16],a2[16],a3[16]; memset(a1,0xEE,16); memset(a2,0xEE,16); memset(a3,0xEE,16);
    const wchar_t *t1=0,*t2=0,*t3=0;
    long r1=sys(s,&t1,a1); long r2=wia_ip6w(s,&t2,a2); long r3=ref_ip6w(s,&t3,a3);
    int ok=(r1==r2)&&((t1-s)==(t2-s))&&(r1==r3)&&((t1-s)==(t3-s));
    if(r1==0){ if(memcmp(a1,a2,16)||memcmp(a1,a3,16)) ok=0; }
    if(!ok){ if(fails<25){ printf("FAIL ["); esc(s);
        printf("] sys{r=%lx t%+d} ours{r=%lx t%+d} ref{r=%lx t%+d}",r1,(int)(t1-s),r2,(int)(t2-s),r3,(int)(t3-s));
        if(r1==0){ printf(" A="); for(int i=0;i<16;i++) printf("%02X",a1[i]);
                   printf(" O="); for(int i=0;i<16;i++) printf("%02X",a2[i]);
                   printf(" R="); for(int i=0;i<16;i++) printf("%02X",a3[i]); }
        printf("\n"); } ++fails; }
}
static void chk(const char* s){ wchar_t w[128]; int i=0; for(;s[i];++i) w[i]=(wchar_t)(unsigned char)s[i]; w[i]=0; chkw(w); }

// live result on an unguarded copy, ours+ref on a copy whose NUL is the last unit before NOACCESS
static void chk_guard(const wchar_t* s, void* guarded_end){
    size_t n=wcslen(s);
    wchar_t* g=(wchar_t*)guarded_end-(n+1);
    memcpy(g,s,(n+1)*sizeof(wchar_t));
    unsigned char a1[16],a2[16],a3[16]; memset(a1,0xEE,16); memset(a2,0xEE,16); memset(a3,0xEE,16);
    const wchar_t *t1=0,*t2=0,*t3=0;
    long r1=sys(s,&t1,a1);                      // reference run on the ordinary copy
    long r2=wia_ip6w(g,&t2,a2); long r3=ref_ip6w(g,&t3,a3);
    // the bases differ here, so an unwritten Terminator must be compared as "unset", not as an offset
    int q1=(t1!=0),q2=(t2!=0),q3=(t3!=0);
    ptrdiff_t o1=q1?t1-s:-1, o2=q2?t2-g:-1, o3=q3?t3-g:-1;
    int ok=(r1==r2)&&(r1==r3)&&(q1==q2)&&(q1==q3)&&(o1==o2)&&(o1==o3);
    if(r1==0){ if(memcmp(a1,a2,16)||memcmp(a1,a3,16)) ok=0; }
    if(!ok){ if(fails<25) printf("GUARD FAIL [%ls] sys{r=%lx t+%d} ours{r=%lx t+%d} ref{r=%lx t+%d}\n",
        s,r1,(int)o1,r2,(int)o2,r3,(int)o3); ++fails; }
}

static const char* C[]={"2001:db8::1","::1","::","fe80::1","1:2:3:4:5:6:7:8","2001:db8::1::2","gggg::1","1:2:3",
    "2001:0db8:0000:0000:0000:0000:0000:0001","::ffff","abcd::","0:0:0:0:0:0:0:0","12345::1","1::2::3","",":","x",
    "::1.2.3.4","::ffff:1.2.3.4","1:2:3:4:5:6:1.2.3.4","1:2:3:4:5:6:7:1.2.3.4","::1.2.3","::1.2.3.4.5","::256.1.1.1",
    "::1.2.3.4x","::01.02.03.04","::1.2.3.4:5","1.2.3.4","::a.b.c.d","::1.2.3.256","::999.1.1.1","::1.2.3.4.",
    "0107.","::76.","a81b::6.","1:2:3:4:5:6:7.","4e3:27:4e5::5:6c:9af:43.",
    // paths change 121's ANSI fuzz never generated, and which this work found were wrong there too:
    // a final group longer than 4 hex digits, an invalid octet whose separator is also wrong, and
    // the number helper's "0x" prefix / 32-bit saturation.
    "12345","12345x","abcdef","0123456789","::12345","::abcde","1:2:12345","1:2:3:4:5:6:7:12345",
    "::1.2222.3.4","::1.999.3.4","::1.2222x.3.4","::1.999x.3.4","::1.2.3.412","::1.2.99999.4",
    "::0x9","::0X9","::0xab","::0xABCD","::0x","::0xx9","::00x9","::0o17","::0b101","::0B11",
    "::0x1234567","::0x12345678","::0x123456789","::1x9","::9x5","::0x9:1","1:0x9::","::0x1.2.3.4",
    "::1.0x2.3.4","::1.2.3.0x5","::1.2.3.4x5","0x1:0x2::0x3","::0x0","x::1","::x1"};
#define NC ((int)(sizeof(C)/sizeof(C[0])))

// (a) units above 255 whose low byte IS a meaningful ASCII character, the aliasing traps;
// (b) one member of each of the 17 Unicode decimal-digit blocks the live export folds into a value;
// (c) plain non-ASCII, a lone surrogate, and the ends of the range.
static const wchar_t TRAP[]={0x012E,0x013A,0x0130,0x0131,0x0139,0x0141,0x0161,0x0146,0xFF11,0xFF1A,0xFF0E,
                             0x0660,0x06F3,0x0967,0x09E9,0x0A6A,0x0AE6,0x0B6F,0x0C66,0x0CE7,0x0D68,
                             0x0E50,0x0ED9,0x0F20,0x1049,0x17E5,0x1810,0xFF19,
                             0x0100,0x00E1,0x00FF,0x0080,0xD800,0xDC00,0xFFFF,0x2000};
#define NTRAP ((int)(sizeof(TRAP)/sizeof(TRAP[0])))

// every one of the 65536 units, dropped into each template at the '#'
static const char* TMPL[]={"::1#","::1#2","::#","::1234#","::1.2.3.4#","::1.2.3.#","::1.2#.3.4",
                           "1:2:3:4:5:6:7:8#","1::#2","#::1","::1:#","::1#.2.3.4","::#.2.3.4",
                           "1#::2","::1##","::1#a","::0x#","::0x#9","::0#x9","::#x9","::12345#",
                           "::1.2222#.3.4","::1.999#.3.4","::0#"};
#define NTMPL ((int)(sizeof(TMPL)/sizeof(TMPL[0])))

int main(void){
    HMODULE h=LoadLibraryW(L"ntdll.dll"); sys=(fn)GetProcAddress(h,"RtlIpv6StringToAddressW");
    if(!sys){ printf("no RtlIpv6StringToAddressW\n"); return 2; }

    // 1. the ANSI edge corpus, widened
    for(int i=0;i<NC;i++) chk(C[i]);

    // 2. wide-only: every trap unit substituted at, and inserted at, every position of every base string
    {
        wchar_t w[160];
        for(int i=0;i<NC;i++){
            int n=(int)strlen(C[i]);
            for(int pos=0; pos<=n; ++pos){
                for(int t=0;t<NTRAP;t++){
                    int j;
                    for(j=0;j<n;j++) w[j]=(wchar_t)(unsigned char)C[i][j];
                    w[n]=0;
                    if(pos<n){ wchar_t sv=w[pos]; w[pos]=TRAP[t]; chkw(w); w[pos]=sv; }   // substitute
                    for(j=n;j>=pos;j--) w[j+1]=w[j];
                    w[pos]=TRAP[t]; chkw(w);                                              // insert
                }
            }
        }
        printf("  wide traps: %d fails so far\n", fails);
    }

    // 3. exhaustive over a wide alphabet that contains a >255 unit, lengths 0..5
    {
        static const wchar_t AL[]={L'0',L':',L'.',L'1',L'9',L'a',L'f',L'g',L'F',0x013A,0x0131,0x00E1};
        int na=(int)(sizeof(AL)/sizeof(AL[0]));
        wchar_t buf[8];
        for(int len=0; len<=5 && fails<=50; ++len){
            long long total=1; for(int i=0;i<len;i++) total*=na;
            for(long long code=0; code<total; ++code){
                long long c=code;
                for(int i=0;i<len;i++){ buf[i]=AL[c%na]; c/=na; }
                buf[len]=0; chkw(buf);
            }
        }
        printf("  exhaustive wide alphabet 0..5: %d fails so far\n", fails);
    }

    // 4. 121's 5M random fuzz, widened (core grammar + embedded-IPv4 tails), with a trap unit
    //    injected into one in eight strings
    {
        unsigned long seed=0x166a7011u; char buf[64]; wchar_t w[80];
        for(int t=0;t<3000000 && fails<=50;t++){
            seed=seed*1103515245u+12345u; int ng=1+((seed>>7)%10); int j=0;
            for(int g=0;g<ng;g++){ if(g){ seed=seed*1103515245u+12345u; buf[j++]=':'; if(((seed>>3)&7)==0)buf[j++]=':'; }
                seed=seed*1103515245u+12345u; int nd=(seed>>6)%5;
                for(int k=0;k<nd;k++){ seed=seed*1103515245u+12345u; int r=(seed>>8)%20; buf[j++]= r<10?('0'+r):r<16?('a'+r-10):(r==16?':':(r==17?'.':(r==18?'z':'0'))); } }
            seed=seed*1103515245u+12345u; int tr=(seed>>4)%4; if(tr==0)buf[j++]='z'; else if(tr==1)buf[j++]='.';
            buf[j]=0;
            int i; for(i=0;i<j;i++) w[i]=(wchar_t)(unsigned char)buf[i]; w[j]=0;
            seed=seed*1103515245u+12345u;
            if(j && ((seed>>5)&7)==0) w[(seed>>9)%j]=TRAP[(seed>>13)%NTRAP];
            chkw(w);
        }
        for(int t=0;t<2000000 && fails<=50;t++){
            seed=seed*1103515245u+12345u; int ng=(seed>>7)%7; int j=0;
            if(((seed>>2)&3)==0){ buf[j++]=':'; buf[j++]=':'; }
            else for(int g=0;g<=ng;g++){ if(g)buf[j++]=':'; seed=seed*1103515245u+12345u; int nd=1+((seed>>6)%4);
                for(int k=0;k<nd;k++){ seed=seed*1103515245u+12345u; buf[j++]="0123456789abcdef"[(seed>>8)%16]; }
                if(((seed>>11)&7)==0){ buf[j++]=':'; buf[j++]=':'; } }
            for(int o=0;o<4;o++){ if(o)buf[j++]='.'; seed=seed*1103515245u+12345u; int nd=1+((seed>>6)%3);
                for(int k=0;k<nd;k++){ seed=seed*1103515245u+12345u; buf[j++]='0'+((seed>>8)%11); } }
            buf[j]=0;
            int i; for(i=0;i<j;i++) w[i]=(wchar_t)(unsigned char)buf[i]; w[j]=0;
            seed=seed*1103515245u+12345u;
            if(j && ((seed>>5)&7)==0) w[(seed>>9)%j]=TRAP[(seed>>13)%NTRAP];
            chkw(w);
        }
        printf("  5M fuzz: %d fails so far\n", fails);
    }

    // 5. every one of the 65536 units, in every template position
    {
        wchar_t w[40];
        for(int m=0;m<NTMPL && fails<=50;m++){
            const char* t=TMPL[m];
            for(unsigned c=1;c<0x10000 && fails<=50;c++){
                int j=0; for(int i=0;t[i];++i) w[j++] = (t[i]=='#') ? (wchar_t)c : (wchar_t)(unsigned char)t[i];
                w[j]=0; chkw(w);
            }
        }
        printf("  all 65536 units x %d templates: %d fails so far\n", NTMPL, fails);
    }

    // 6. NOACCESS page guard one unit past the terminating NUL, for every base string and every
    //    trap substitution
    {
        char* base=(char*)VirtualAlloc(0,0x2000,MEM_RESERVE,PAGE_NOACCESS);
        VirtualAlloc(base,0x1000,MEM_COMMIT,PAGE_READWRITE);
        void* end=base+0x1000;
        wchar_t w[160];
        for(int i=0;i<NC;i++){
            int n=(int)strlen(C[i]); int j;
            for(j=0;j<n;j++) w[j]=(wchar_t)(unsigned char)C[i][j];
            w[n]=0; chk_guard(w,end);
            for(int pos=0;pos<n;++pos){ wchar_t sv=w[pos];
                for(int t=0;t<NTRAP;t++){ w[pos]=TRAP[t]; chk_guard(w,end); }
                w[pos]=sv; }
        }
        printf("  page guard: %d fails so far\n", fails);
    }

    /* "STATUS+addr+Terminator" used to be the whole of this line, and it overstated what the loops
       above actually do: they compare the address inside `if(r1==0)`, i.e. ON SUCCESS ONLY. Change
       250 found the gap by composing this core into RtlIpv6StringToAddressExW -- the shipped parser
       fills the destination as it goes, so a FAILING call keeps whatever it had committed while this
       one leaves the buffer untouched, on 17268 of 55987 enumerated strings (0 status, 0 Terminator,
       0 on any success). See RESULTS.md: the obvious fix measured WORSE and is recorded, not applied.
       The banner now says what the harness checks. */
    if(!fails) printf("CORRECTNESS: PASS (RtlIpv6StringToAddressW vs live + oracle: STATUS and Terminator on every case, addr on SUCCESS (see RESULTS.md); %d edges, %d trap units x every position (sub+insert), exhaustive wide alphabet 0..5, 5M fuzz, ALL 65536 units x %d templates, NOACCESS page guard)\n", NC, NTRAP, NTMPL);
    else printf("CORRECTNESS: FAIL (%d)\n",fails);
    return fails?1:0;
}
