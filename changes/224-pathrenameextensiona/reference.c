// changes/224-pathrenameextensiona/reference.c
// Independent oracle for shlwapi!PathRenameExtensionA -- the narrow sibling of change 158.
//
// Every rule here was re-derived against the narrow export (probes/ren.c), not inherited from the
// wide form. That distinction is not academic in this repository: change 158 SHIPPED WRONG. Its
// extension position is change 132's rule, and that rule was missing the SPACE stopper -- it was
// wrong on 46158 of 335923 enumerated strings until it was corrected in this session, alongside
// 132, 140, 143, 144, 159, 160 and 174. Eight landed changes, one missing rule, and the lesson was
// that a shared rule has to be audited by what the code COMPUTES rather than by what it cites.
//
// What the narrow export actually does, measured:
//
//   * The extension is at the last '.' after the last backslash or space. '/' and ':' do not stop
//     the search ("a.b/c" + ".obj" -> "a.obj"), and neither does a TAB -- the stopper is 0x20
//     specifically. With no extension the position is the terminator, so the extension appends.
//   * The MAX_PATH limit bounds the result, not the input. Swept over extension lengths 1..6 and
//     input lengths 240..275: the first FALSE moves with the extension length, and the last
//     successful RESULT length is 259 in all six sweeps.
//   * On failure the buffer is UNTOUCHED, so the decision must precede the first store.
//   * The extension is not validated: all 255 non-NUL byte values are copied verbatim, including a
//     space, a backslash and a non-leading dot. The PathCch siblings (changes 159 and 160) reject
//     exactly those three; this one does not.
//   * NULL path -> FALSE without faulting. NULL extension -> FALSE, buffer untouched.
//   * Only the extension and its terminator are written; the tail past it is left stale.
#include <windows.h>

static int ref_extpos(const char* p)
{
    int n = 0;
    while (p[n]) ++n;
    int cand = -1;
    for (int i = 0; i < n; ++i) {
        if (p[i] == '\\' || p[i] == ' ') cand = -1;   /* the SPACE stops it, as the backslash does */
        else if (p[i] == '.') cand = i;
    }
    return cand < 0 ? n : cand;                       /* no extension -> the terminator */
}

BOOL ref_pathrenameexta(char* path, const char* ext)
{
    if (!path) return FALSE;
    if (!ext)  return FALSE;

    int pos = ref_extpos(path);
    int elen = 0;
    while (ext[elen]) ++elen;

    if (pos + elen > 259) return FALSE;               /* the RESULT must fit; buffer untouched */

    char* d = path + pos;
    for (int i = 0; i <= elen; ++i) d[i] = ext[i];    /* verbatim, terminator included */
    return TRUE;
}
