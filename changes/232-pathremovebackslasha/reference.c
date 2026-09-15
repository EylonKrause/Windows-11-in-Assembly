// changes/232-pathremovebackslasha/reference.c
// Independent oracle for shlwapi!PathRemoveBackslashA.
//
// Every rule re-derived against the NARROW export in probes/prba.c, not inherited from change 171:
//
//   * the return is ALWAYS psz + max(n-1, 0) -- the LAST CHARACTER, not the terminator;
//   * one trailing backslash is removed unless the remainder would be a bare root:
//         m == 0, or (m == 1 and psz[0] == BACKSLASH), or (m == 2 and psz[1] == ':' and a letter)
//   * EXACTLY ONE byte value is ever removed: 0x5C. Sweeping all 255 non-NUL values as the trailing
//     character, only the backslash goes -- a FORWARD SLASH IS NOT A SEPARATOR;
//   * NULL returns NULL;
//   * 0 mismatches over all 19531 strings of {a, BACKSLASH, /, :, C} to length 6.
//
// THE DRIVE-LETTER SET IS ASCII ONLY, AND THAT IS WHERE THE NARROW FORM DIFFERS FROM THE WIDE ONE.
// Change 171 pinned the wide set by an exhaustive 65535-code-unit sweep and found the ASCII letters
// PLUS the Latin-1 letters. Sweeping all 255 byte values here gives exactly 0x41..0x5A and
// 0x61..0x7A -- 52 values in two runs, nothing above 0x7A. Inheriting the wide set would have
// produced a function that wrongly protects 78 byte values.

static int drive_letter(unsigned char c){
    return (c >= 0x41 && c <= 0x5A) || (c >= 0x61 && c <= 0x7A);
}

char* ref_pathremovebackslasha(char* psz){
    if (!psz) return 0;
    int n = 0;
    while (psz[n]) ++n;
    char* ret = psz + (n ? n - 1 : 0);
    if (n > 0 && psz[n-1] == 0x5C) {
        int m = n - 1;
        int prot = (m == 0)
                || (m == 1 && (unsigned char)psz[0] == 0x5C)
                || (m == 2 && psz[1] == ':' && drive_letter((unsigned char)psz[0]));
        if (!prot) psz[n-1] = 0;
    }
    return ret;
}
