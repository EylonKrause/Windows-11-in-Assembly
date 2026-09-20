// changes/294-strchr/probes/shapes.c -- driver for shapes.asm. See that file's header.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bench.h"

typedef char* (__cdecl *fn)(const char*, int);
extern char* wia_a1(const char*,int);
extern char* wia_a2(const char*,int);
extern char* wia_a3(const char*,int);
extern char* wia_a4(const char*,int);
extern char* wia_b2(const char*,int);
extern char* wia_b3(const char*,int);
static fn sysu;

static char* ref(const char* s, int c){
    unsigned char n=(unsigned char)(c&0xFF);
    for(;;){ if((unsigned char)*s==n) return (char*)s; if(!*s) return 0; ++s; }
}

typedef struct { const char* s; int c; fn f; } ctx_t;
static uint64_t op(void* p){ ctx_t* x=(ctx_t*)p; return (uint64_t)(uintptr_t)x->f(x->s,x->c); }

#define NV 7
static fn V[NV]      = { 0, wia_a1, wia_a2, wia_a3, wia_a4, wia_b2, wia_b3 };
static const char* VN[NV]={"sys","a1","a2","a3","a4","b2","b3"};

static const size_t L[]={1,3,7,15,17,23,31,33,47,63,95,127,255,1023,8191,65535};
#define NL (sizeof(L)/sizeof(L[0]))

int main(void){
    sysu=(fn)GetProcAddress(LoadLibraryW(L"ucrtbase.dll"),"strchr");
    V[0]=sysu;

    // --- quick correctness screen so no broken variant gets timed -----------------------------
    {
        static char b[4096]; unsigned long long r=0x1234567ULL; int bad=0;
        for(int it=0; it<60000; ++it){
            r=r*6364136223846793005ULL+1ULL;
            unsigned len=(unsigned)((r>>33)%300), off=(unsigned)((r>>13)%48);
            char* s=b+off;
            for(unsigned i=0;i<len;++i){ r=r*6364136223846793005ULL+1ULL; unsigned v=(unsigned)((r>>33)&0xFF); s[i]=(char)(v?v:7);}
            s[len]=0;
            r=r*6364136223846793005ULL+1ULL;
            int c = (int)((r>>33)&0xFF);
            if(((r>>20)&3)==0) c=0;
            if(len && ((r>>21)&3)==1) c=(unsigned char)s[(r>>25)%len];
            char* want=ref(s,c);
            if(sysu(s,c)!=want){ printf("SYS MISMATCH\n"); bad=1; }
            for(int v=1; v<NV; ++v) if(V[v](s,c)!=want){ printf("VARIANT %s MISMATCH len=%u off=%u c=%d\n",VN[v],len,off,c); bad=1; break; }
            if(bad) return 1;
        }
        // page guard for every variant
        SYSTEM_INFO si; GetSystemInfo(&si); DWORD pg=si.dwPageSize;
        unsigned char* a=(unsigned char*)VirtualAlloc(NULL,pg*3,MEM_RESERVE,PAGE_NOACCESS);
        VirtualAlloc(a+pg,pg,MEM_COMMIT,PAGE_READWRITE);
        for(int tail=1;tail<=200;++tail){
            char* term=(char*)(a+2*pg-tail); char* s=term-256;
            memset(s,'A',256); *term=0;
            char* want=ref(s,'Q');
            for(int v=1;v<NV;++v) if(V[v](s,'Q')!=want){ printf("VARIANT %s PAGE FAULT/MISMATCH tail=%d\n",VN[v],tail); return 1; }
        }
        printf("variant screen: OK\n\n");
    }

    wia_pin(2);
    volatile uint64_t sink=0;

    static const int OFF[3] = {0,16,5};
    for(int mo=0; mo<6; ++mo){
        int mode=mo/3, oi=mo%3;
        printf("== %s, start offset %d mod 32 ==\n",
               mode? "needle FOUND at last char" : "needle ABSENT", OFF[oi]);
        printf("%-8s","size");
        for(int v=0;v<NV;++v) printf("%10s",VN[v]);
        printf("   best\n");
        for(size_t i=0;i<NL;++i){
            size_t n=L[i];
            char* s=(char*)_aligned_malloc(n+256,4096);
            s+=OFF[oi];
            for(size_t j=0;j<n;++j) s[j]=(char)('a'+(j&15));
            if(mode&&n) s[n-1]='@';
            s[n]=0;
            printf("%-8zu",n);
            double t[NV]; double best=1e300; int bi=0;
            for(int v=0;v<NV;++v){
                ctx_t cx={s,'@',V[v]};
                t[v]=wia_measure(op,&cx,300,&sink);
                printf("%10.2f",t[v]);
                if(v&&t[v]<best){best=t[v];bi=v;}
            }
            printf("   %s %.2fx\n", VN[bi], t[0]/best);
        }
        printf("\n");
    }
    printf("sink=%llu\n",(unsigned long long)sink);
    return 0;
}
