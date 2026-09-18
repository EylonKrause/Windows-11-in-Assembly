/* discovery/rtlrandomex_is_not_a_function_of_its_seed.c
 *
 * ntdll!RtlRandomEx CANNOT BE REIMPLEMENTED BIT-EXACTLY. This file is the measurement that kills it, in
 * the shape of discovery/lstrcmp_is_linguistic.c and discovery/strstra_not_bytewise.c -- a candidate that
 * looked excellent on the numbers and died on its contract, recorded rather than deleted so it is not
 * picked up again.
 *
 * ------------------------------------------------------------------------------------------------------
 * WHY IT LOOKED LIKE THE BEST TARGET LEFT
 *
 * discovery/uncovered_2026b.c timed two PRNGs with the same harness, the same prototype `ULONG f(ULONG*)`
 * and the same call shape:
 *
 *      ntdll!RtlUniform      1.18 ns
 *      ntdll!RtlRandomEx    27.99 ns          <-- 24x, the largest unexplained ratio in that sweep
 *
 * RtlUniform is one Lehmer step and 1.18 ns is about right for a multiply, an add and a reduction. 28 ns
 * is roughly 100 cycles, which is not arithmetic. A 24x gap between two functions with the same signature
 * is exactly the shape of a function that is slow for a removable reason, and those are the best targets
 * this project has.
 *
 * ------------------------------------------------------------------------------------------------------
 * WHAT IT ACTUALLY IS: THE RETURNED VALUE IS NOT A FUNCTION OF THE SEED
 *
 * Measured, in one process:
 *
 *      randomex(12345) -> C618F93F 16E5119C F6A1082C D2C5A684 ...
 *      randomex(12345) -> 170D7DFF 1529DBDA E7DB975B EE17495A ...     the SAME seed, different answers
 *
 * The seed is the only documented input and the same seed gives different output. There is hidden state,
 * it is mutated by every call, and it is shared: interleaving calls on two INDEPENDENT `ULONG` seeds
 * changes what each of them returns. That is the documented 128-entry shuffle table, which lives in
 * ntdll's data and therefore belongs to the whole process, not to the caller's seed.
 *
 * A bit-exact replacement would have to reproduce that table, which means knowing its initial contents AND
 * every RtlRandomEx call any code in the process has made since -- including calls made by code that is
 * not ours. That is not a function this project can reimplement; it is not a function at all in the sense
 * the gates require.
 *
 * AND IT IS NOT EVEN REPRODUCIBLE ACROSS PROCESSES. The obvious last hope was that the table is seeded
 * deterministically at process start, so that at least the FIRST call with a given seed would be a
 * function of that seed. Section 5 captures exactly that -- the first RtlRandomEx call of the process,
 * taken at the top of main before anything else in this program touches the table -- and two consecutive
 * runs of this same binary give:
 *
 *      run 1:  first call of the process, seed 12345 -> F78ED844, seed left at 7FFC9BC1
 *      run 2:  first call of the process, seed 12345 -> D3287E76, seed left at 7FFC9BC1
 *
 * Different values, identical seed update. The table is seeded from something unpredictable at process
 * start, so there is no call anywhere -- not even the first -- whose return value is determined by the
 * documented input. The kill is total.
 *
 * (The first draft of section 5 sat where its number belonged, AFTER sections 2-4, and therefore measured
 * nothing: the table it wanted to observe had already been advanced sixty-odd times by the sections above
 * it. A check placed after the state it means to observe has been mutated is not a check. It was moved to
 * the top of main.)
 *
 * AND THE GATE MUST NOT BE WEAKENED TO ACCOMMODATE IT. The tempting move is to gate on distribution or on
 * the seed update instead of on the value, and that is precisely the "gate that cannot fail" this project
 * refused for change 288's missing live substitution. A gate that cannot fail is not evidence.
 *
 * ------------------------------------------------------------------------------------------------------
 * THE ONE GENUINELY USEFUL FINDING: THE SEED UPDATE *IS* RtlUniform, EXACTLY
 *
 *      uniform (         1) -> 7FFFFFB1, seed becomes 7FFFFFB1
 *      randomex(         1) -> 5214F3A1, seed becomes 7FFFFFB1     <-- same seed out, different value
 *      uniform (     12345) -> 7FFC9BC1, seed becomes 7FFC9BC1
 *      randomex(     12345) -> A041B84C, seed becomes 7FFC9BC1
 *      uniform (2147483646) -> 7FFFFFD5, seed becomes 7FFFFFD5
 *      randomex(2147483646) -> 077939D0, seed becomes 7FFFFFD5
 *
 * So RtlRandomEx advances the caller's seed with exactly one RtlUniform step and then returns something
 * else entirely -- a value pulled out of the shuffle table, with the fresh Lehmer output stirred back in.
 * The seed trajectory is reproducible and the return value is not. Section 4 below sweeps that over
 * thousands of seeds rather than asserting it from three.
 *
 * That is worth writing down because it bounds what a caller can rely on, and because 28 ns to advance a
 * seed that RtlUniform advances identically in 1.2 ns is a real fact about the shipped export.
 *
 * build:  cl /nologo /O2 rtlrandomex_is_not_a_function_of_its_seed.c /Fe:rtlrandomex_not_a_function.exe
 * run it TWICE: section 5's first line differs between runs, and that is the point. The program exits
 * non-zero if its own self-checks fail, so a conclusion drawn from a broken measurement is visible.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef ULONG (WINAPI *F_rng)(ULONG*);

static F_rng uniform, randomex;
static LARGE_INTEGER freq;
static int failures;

static double bench_chain(F_rng g, int iters)
{
    LARGE_INTEGER a, b;
    ULONG seed = 12345, sink = 0;
    int i;
    for (i = 0; i < 20000; ++i) sink += g(&seed);
    QueryPerformanceCounter(&a);
    for (i = 0; i < iters; ++i) sink += g(&seed);
    QueryPerformanceCounter(&b);
    if (sink == 0xFFFFFFFFul) printf("");
    return (double)(b.QuadPart - a.QuadPart) * 1e9 / (double)freq.QuadPart / iters;
}

static volatile LONG go;
static double thread_ns;

static DWORD WINAPI worker(LPVOID p)
{
    while (!go) Sleep(0);
    thread_ns = bench_chain((F_rng)p, 2000000);
    return 0;
}

int main(void)
{
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    int i, k;
    ULONG first_seed = 12345, first_value, first_seed_after;

    QueryPerformanceFrequency(&freq);
    uniform  = (F_rng)GetProcAddress(nt, "RtlUniform");
    randomex = (F_rng)GetProcAddress(nt, "RtlRandomEx");
    if (!uniform || !randomex) { printf("not exported\n"); return 2; }

    /* THE VERY FIRST RtlRandomEx CALL OF THIS PROCESS, captured before anything else in this program
       touches the shuffle table. It has to happen here, at the top of main, because every later call
       advances the table -- the first draft of this file put the check in section 5 and it measured
       nothing at all, which is worth stating: a section placed after the state it wants to observe has
       already been mutated is not a measurement. Reported in section 5 below. */
    first_value = randomex(&first_seed);
    first_seed_after = first_seed;

    printf("== ntdll!RtlRandomEx: why the 24x gap is not a target ==\n");

    /* --- 1. the gap, reproduced ---------------------------------------------------------------- */
    printf("\n-- 1. the gap, reproduced with a chained seed (a real caller's shape)\n");
    printf("   RtlUniform  %7.2f ns      RtlRandomEx %7.2f ns\n",
           bench_chain(uniform, 2000000), bench_chain(randomex, 2000000));

    /* --- 2. THE KILL: the same seed twice ------------------------------------------------------ */
    printf("\n-- 2. THE KILL. The same seed, twice, in one process. The seed is the only documented\n");
    printf("      input, so the same seed must give the same value -- for RtlUniform it does.\n");
    {
        ULONG s1, s2, a[6], b[6];
        int rand_same = 1, unif_same = 1;
        s1 = 12345; for (i = 0; i < 6; ++i) a[i] = randomex(&s1);
        s2 = 12345; for (i = 0; i < 6; ++i) b[i] = randomex(&s2);
        for (i = 0; i < 6; ++i) if (a[i] != b[i]) rand_same = 0;
        printf("      RtlRandomEx  run A:");
        for (i = 0; i < 6; ++i) printf(" %08lX", a[i]);
        printf("\n      RtlRandomEx  run B:");
        for (i = 0; i < 6; ++i) printf(" %08lX", b[i]);
        printf("\n      -> %s\n", rand_same ? "identical" : "DIFFERENT, from the same seed");

        s1 = 12345; for (i = 0; i < 6; ++i) a[i] = uniform(&s1);
        s2 = 12345; for (i = 0; i < 6; ++i) b[i] = uniform(&s2);
        for (i = 0; i < 6; ++i) if (a[i] != b[i]) unif_same = 0;
        printf("      RtlUniform   run A:");
        for (i = 0; i < 6; ++i) printf(" %08lX", a[i]);
        printf("\n      RtlUniform   run B:");
        for (i = 0; i < 6; ++i) printf(" %08lX", b[i]);
        printf("\n      -> %s\n", unif_same ? "identical, as a pure function must be" : "DIFFERENT");

        if (rand_same || !unif_same) {
            printf("      *** the measurement did not reproduce; do not trust the conclusion ***\n");
            ++failures;
        } else {
            printf("      RtlRandomEx IS NOT A FUNCTION OF ITS SEED. Nothing this project can gate on.\n");
        }
    }

    /* --- 3. the hidden state is SHARED across seeds -------------------------------------------- */
    printf("\n-- 3. the hidden state is shared across independent seeds. Two separate ULONG variables,\n");
    printf("      one read straight through and one with unrelated calls interleaved.\n");
    {
        ULONG sA = 7, sB = 7, clean[5], dirty[5], junk = 999;
        int same = 1;
        for (i = 0; i < 5; ++i) clean[i] = randomex(&sA);
        for (i = 0; i < 5; ++i) { (void)randomex(&junk); dirty[i] = randomex(&sB); }
        for (i = 0; i < 5; ++i) if (clean[i] != dirty[i]) same = 0;
        printf("      seed 7 alone      :");
        for (i = 0; i < 5; ++i) printf(" %08lX", clean[i]);
        printf("\n      seed 7 interleaved:");
        for (i = 0; i < 5; ++i) printf(" %08lX", dirty[i]);
        printf("\n      -> %s\n", same ? "unaffected" :
               "calls on an UNRELATED seed changed the answers: the state is process-global");
    }

    /* --- 4. but the SEED UPDATE is exactly RtlUniform, over thousands of seeds ----------------- */
    printf("\n-- 4. the seed update, swept. For every seed below, does RtlRandomEx leave the seed at\n");
    printf("      exactly the value RtlUniform would? (The VALUE it returns is a different question.)\n");
    {
        long checked = 0, seed_diff = 0, value_same = 0;
        static const ULONG probes[] = { 0, 1, 2, 3, 12345, 0x10000, 0x7FFFFFFD, 0x7FFFFFFE,
                                        0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF };
        ULONG s;
        for (k = 0; k < (int)(sizeof(probes) / sizeof(probes[0])); ++k) {
            ULONG su = probes[k], sr = probes[k], vu, vr;
            s = probes[k];
            vu = uniform(&su);
            vr = randomex(&sr);
            ++checked;
            if (su != sr) {
                ++seed_diff;
                printf("      seed %08lX: uniform leaves %08lX, randomex leaves %08lX  DIFFER\n",
                       probes[k], su, sr);
            }
            if (vu == vr) ++value_same;
            (void)s; (void)vu; (void)vr;
        }
        /* and a wide sweep, values ignored */
        for (k = 0; k < 20000; ++k) {
            ULONG base = (ULONG)k * 214013ul + 2531011ul;
            ULONG su = base, sr = base;
            (void)uniform(&su);
            (void)randomex(&sr);
            ++checked;
            if (su != sr) ++seed_diff;
        }
        printf("      %ld seeds checked, %ld where the seed ends up different, %ld where the returned\n"
               "      value happened to match\n", checked, seed_diff, value_same);
        printf("      -> %s\n", seed_diff == 0
               ? "the seed update IS exactly one RtlUniform step, on every seed tried"
               : "*** the seed update is NOT plain RtlUniform ***");
        if (seed_diff != 0) ++failures;
    }

    /* --- 5. across PROCESSES: is the table at least initialised deterministically? ------------- */
    printf("\n-- 5. the very first RtlRandomEx call in this process, seed 12345. Run this program twice:\n");
    printf("      if the line below changes between runs, the shuffle table is seeded from something\n");
    printf("      unpredictable and even the first call is not reproducible. Either way a replacement\n");
    printf("      would have to track every call made anywhere in the process, so this only sharpens\n");
    printf("      the conclusion rather than changing it.\n");
    printf("      FIRST CALL OF THIS PROCESS, seed 12345 -> %08lX, seed left at %08lX\n",
           first_value, first_seed_after);
    printf("      (captured at the top of main, before any other call in this program touched the\n");
    printf("       table -- the first draft checked this in place, AFTER sections 2-4 had already\n");
    printf("       advanced the table, and so measured nothing)\n");
    {
        ULONG u = 12345;
        ULONG uv = uniform(&u);
        printf("      for comparison, RtlUniform(12345) -> %08lX, seed left at %08lX\n", uv, u);
        printf("      -> the seed matches (%s) and the value does not, on the very first call too\n",
               u == first_seed_after ? "yes" : "NO");
        if (u != first_seed_after) ++failures;
    }

    /* --- 6. what the 28 ns is made of ---------------------------------------------------------- */
    printf("\n-- 6. and where the 28 ns goes, for the record. The state is process-global and mutated\n");
    printf("      every call, so its cache line is written by every thread that calls in.\n");
    {
        double solo, together;
        HANDLE h;
        go = 0;
        solo = bench_chain(randomex, 2000000);
        h = CreateThread(0, 0, worker, (LPVOID)randomex, 0, 0);
        go = 1;
        together = bench_chain(randomex, 2000000);
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
        printf("      solo %7.2f ns   two threads %7.2f / %7.2f ns   (%.2fx)\n",
               solo, together, thread_ns, together / solo);
        printf("      a rise here is the shared line bouncing, not a lock: there is no serialisation,\n");
        printf("      just a read-modify-write on memory every caller in the process shares.\n");
    }

    printf("\n== CONCLUSION: RtlRandomEx is NOT REIMPLEMENTABLE. Its return value is not a function of\n");
    printf("   its documented input. The seed update alone is exactly RtlUniform, which is already\n");
    printf("   24x faster; a caller who needs only a seed advanced should call that instead. Do not\n");
    printf("   pick this export up again, and do not weaken a gate to make it fit. ==\n");
    printf("   (measurement self-checks failed: %d -- must be 0 for the conclusion to stand)\n", failures);
    return failures ? 1 : 0;
}
