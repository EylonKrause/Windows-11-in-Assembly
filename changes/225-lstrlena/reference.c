// changes/225-lstrlena/reference.c
// Independent oracle for kernelbase!lstrlenA.
//
// Everything below was measured in probes/lena.c against the live export, not assumed:
//
//   * it is a plain byte scan. Only 0x00 terminates. All 255 non-NUL byte values were placed at
//     the first byte, in the middle, and immediately before the terminator -- 0 of 765 placements
//     disagree. Acp 1252 has zero dbcs lead bytes (GetCPInfo), so there is no mbcs rule to respect;
//   * NULL returns 0 and does not fault;
//   * an access violation returns 0 -- not the partial length, at any distance from the guard;
//   * lengths 0..300 and a 1 MB string are all exact, and every start offset in a 64-byte window
//     agrees, so nothing depends on alignment.
//
// This oracle is deliberately the dumbest possible loop. Its job is to disagree with the
// implementation when the implementation is clever and wrong.

int ref_lstrlena(const char* psz)
{
    int n = 0;
    if (psz == 0) return 0;
    while (psz[n]) ++n;
    return n;
}
