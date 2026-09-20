// changes/235-pathisfilespeca/reference.c
// Independent oracle for shlwapi!PathIsFileSpecA.
//
// Measured in probes/pifsa.c against the live export:
//
//   * Exactly two byte values are separators: 0x5C and 0x3A. Confirmed at the first, middle and
//     LAST positions -- 2 of 255 at each -- so neither is position-dependent. A forward slash is
//     not one: "a/b" and "/" are both TRUE.
//   * The empty string is TRUE, and that is the one case a natural model gets wrong. The first
//     version of the probe's model required a non-empty string and that single case was its only
//     mismatch in 488281 enumerated strings.
//   * NULL returns 0.
//   * 0 mismatches over all 488281 strings of {a, backslash, :, /, 0x80} to length 8.
//   * The shipped export does not overread: 398 of 398 guard-page cases were clean, both with a
//     separator at the front and with none at all.

int ref_pathisfilespeca(const char* psz)
{
    if (!psz) return 0;
    for (int i = 0; psz[i]; ++i)
        if (psz[i] == 0x5C || psz[i] == 0x3A) return 0;
    return 1;                              /* the empty string falls straight through: TRUE */
}
