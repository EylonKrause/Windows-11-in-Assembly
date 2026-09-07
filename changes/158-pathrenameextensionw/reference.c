// changes/158-pathrenameextensionw/reference.c
// Oracle for shlwapi!PathRenameExtensionW. The extension rule is the one validated in change 132:
// the LAST '.' after the last BACKSLASH, with only '\' terminating the search.
#include <windows.h>
#include <wchar.h>

static const wchar_t* ref_findext(const wchar_t* p)
{
    const wchar_t* cand = 0;
    for (; *p; ++p) {
        if (*p == L'\\') cand = 0;
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
