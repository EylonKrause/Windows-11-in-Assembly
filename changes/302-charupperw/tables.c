/* changes/302-charupperw/tables.c
 *
 * The two case tables, built by asking the exports under test themselves, in character mode, one code
 * unit at a time -- the same decision change 277 made for CharUpperBuffW. A transcribed table is a
 * constant nobody can check by reading it; a table built from CharUpperW is CharUpperW's by definition,
 * and correctness.c then proves string mode agrees with it on every code unit.
 *
 * The vector path in impl.asm maps any 16-wchar block with no code unit at or above 0x80 by a range
 * subtract instead of the table, which is only right if the table agrees with that rule below 0x80.
 * That is checked here, all 128, and the build refuses to run if it ever stops being true.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

unsigned short wia_cuw_up[65536];
unsigned short wia_cuw_dn[65536];

int wia_cuw_init(void)
{
    for (int i = 0; i < 65536; ++i) {
        wia_cuw_up[i] = (unsigned short)(UINT_PTR)CharUpperW((LPWSTR)(UINT_PTR)i);
        wia_cuw_dn[i] = (unsigned short)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)i);
    }
    for (int i = 0; i < 0x80; ++i) {
        unsigned up = (i >= 'a' && i <= 'z') ? (unsigned)(i - 0x20) : (unsigned)i;
        unsigned dn = (i >= 'A' && i <= 'Z') ? (unsigned)(i + 0x20) : (unsigned)i;
        if (wia_cuw_up[i] != up) return 2;
        if (wia_cuw_dn[i] != dn) return 3;
    }
    if (wia_cuw_up['a'] != 'A' || wia_cuw_dn['A'] != 'a') return 4;   /* an identity table is wrong */
    return 0;
}
