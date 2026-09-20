// changes/223-pathundecoratea/reference.c
// Independent oracle for shlwapi!PathUndecorateA -- the narrow sibling of change 174.
//
// The rule is change 174'S, with the correction 174 Needed. Four conjuncts, all re-derived against
// the NARROW export in probes/undec.c and probes/bytes.c rather than inherited on the strength of
// the names matching. The decoration "[n]" is removed only when ALL of these hold:
//
//   (a) it is in the LAST COMPONENT -- after the last backslash. "C:\dir[1]\file.txt" is untouched.
//   (b) its ']' is the character immediately before THE EXTENSION -- the last '.' after the last
//       backslash OR SPACE -- or immediately before the end of the string when there is no such
//       dot. This is the conjunct that decides the awkward cases:
//           "a[1].b[2]"    -> "a.b[2]"     (the group before the only dot)
//           "a[1].b[2].c"  -> "a[1].b.c"   (the group before the LAST dot, not the first one)
//           "a[1]x[2]"     -> "a[1]x"      (no dot, so the group before the end)
//           "file[1]x.txt" -> unchanged    (nothing hugs the dot)
//   (c) the contents are decimal digits, and there may be none: "file[].txt" -> "file.txt".
//   (d) the '[' is NOT the first character of the component: "[1].txt" and "x\[1].txt" are left
//       alone, "file[1].txt" and "x\a[1].txt" are not.
//
// The space in (b) Is the whole reason this change exists in this shape. Change 174 shipped without
// it -- it derived its rule over an alphabet with no space in it, so neither its fuzz nor its
// live-substitution driver could see the gap -- and probes/space2.c found it while enumerating this
// narrow export. Over every string of {'[', ']', '.', '0', SPACE, 'z'} of length 0..7:
//
//     live PathUndecorateA vs 174's rule as it shipped : 2724 of 335923 mismatches
//     live PathUndecorateA vs the corrected rule       :    0
//     live PathUndecorateW, the same two questions     : 2724, then 0
//
// so the wide export was carrying it too, and change 174 was corrected in the same session.
//
// Note the asymmetry, because it is easy to get wrong: the space bounds the extension search in (b),
// but it does NOT start a new component for (d). "a b[1].txt" -> "a b.txt": the '[' is not the
// component's first character even though a space precedes the name.
//
// The removal closes the gap by moving the remainder down and -- like the shipped export -- leaves
// the stale tail past the new terminator untouched, which is why every test here compares the whole
// buffer rather than the string.
//
// A byte-wise walk is correct on this system: GetACP() is 1252 and it has ZERO DBCS lead bytes
// (printed by probes/bytes.c), and all 255 non-NUL byte values were swept at each of the six
// positions the rule consults, with 0 disagreements.

void ref_pathundecoratea(char* psz){
    if (!psz) return;

    int n = 0;
    while (psz[n]) ++n;

    /* the component: delimited by the BACKSLASH ALONE */
    int comp = 0;
    for (int i = 0; i < n; i++) if (psz[i] == '\\') comp = i + 1;

    /* the extension: scan back from the end, stopping at a backslash OR A SPACE */
    int ext = n;
    for (int q = n; q > 0; ) {
        --q;
        if (psz[q] == '.') { ext = q; break; }
        if (psz[q] == '\\' || psz[q] == ' ') break;
    }

    if (ext - 1 <= comp) return;          /* no room for a group inside the component */
    if (psz[ext-1] != ']') return;        /* (b) the group must hug the extension      */

    int j = ext - 2;
    while (j > comp && psz[j] >= '0' && psz[j] <= '9') --j;   /* (c) digits, possibly none */
    if (j <= comp) return;                /* (d) the '[' may not start the component   */
    if (psz[j] != '[') return;

    int k = j, m = ext;
    while (psz[m]) psz[k++] = psz[m++];
    psz[k] = 0;                           /* the tail past here is deliberately stale   */
}
