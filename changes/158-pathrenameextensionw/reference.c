// changes/158-pathrenameextensionw/reference.c
// Oracle for shlwapi!PathRenameExtensionW.
//
// CORRECTED 2026-09-15. The extension rule is change 132's, and that rule was INCOMPLETE: a
// SPACE stops the backward scan exactly as a backslash does. This oracle and the
// implementation were wrong together on 46158 of 335923 enumerated strings; see
// discovery/extension_space_audit2.c. The rule is: the LAST '.' after the last BACKSLASH
// **Or space**, with '/' and ':' not terminating the search.
#include <windows.h>
#include <wchar.h>

static const wchar_t* ref_findext(const wchar_t* p)
{
    const wchar_t* cand = 0;
    for (; *p; ++p) {
        if (*p == L'\\' || *p == L' ') cand = 0;   /* CORRECTED: a SPACE stops it too */
        else if (*p == L'.') cand = p;
    }
    return cand ? cand : p;             /* no extension -> the terminator */
}

BOOL ref_pathrenameextw(wchar_t* path, const wchar_t* ext)
{
    if (!ext) return FALSE;
    const wchar_t* at = ref_findext(path);
    size_t pos = (size_t)(at - path);
    size_t elen = 0;
    while (ext[elen]) ++elen;
    if (pos + elen > 259) return FALSE;             /* MAX_PATH - 1; path left untouched */
    wchar_t* d = path + pos;
    for (size_t i = 0; i <= elen; ++i) d[i] = ext[i];
    return TRUE;
}
