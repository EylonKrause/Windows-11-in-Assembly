// changes/217-pathfindextensiona/reference.c
// Oracle for shlwapi!PathFindExtensionA. Not fast; just obviously right.
//
// The rule, verified against the LIVE export with 0 mismatches over 2015539 enumerated strings:
//   the last '.' after the last STOPPER, where a stopper is a BACKSLASH **or a SPACE** -- else a
//   pointer to the terminating NUL. '/' and ':' do NOT stop the search, even though
//   PathFindFileNameA treats all three as separators.
//
// The space half of that rule is the one change 132 shipped without, and probing THIS function is
// what caught it. It is 0x20 specifically and not whitespace in general: "a.b\t" still yields the
// dot. The walk is byte-wise here -- 0 of 254 byte values act as a DBCS lead byte on code page 1252.
#include <windows.h>

const char* ref_pathfindexta(const char* p){
    const char* end = p;
    if (!p) return 0;
    while (*end) end++;
    for (const char* q = end; q > p; ) {
        --q;
        if (*q == '.') return q;
        if (*q == '\\' || *q == ' ') break;
    }
    return end;
}
