# msvcrt.dll — reimplemented exports (34)

| export | speedup | source |
|---|---|---|
| `_i64toa` | 1.61x | [changes/057-i64toa](../../../../../changes/057-i64toa/) |
| `_i64tow` | 1.59x | [changes/075-i64tow](../../../../../changes/075-i64tow/) |
| `_itoa` | 1.44x | [changes/056-itoa](../../../../../changes/056-itoa/) |
| `_itow` | 1.42x | [changes/074-itow](../../../../../changes/074-itow/) |
| `_memicmp` | 10.70x | [changes/046-memicmp](../../../../../changes/046-memicmp/) |
| `_stricmp` | 8.73x | [changes/043-stricmp](../../../../../changes/043-stricmp/) |
| `_strnicmp` | 9.31x | [changes/045-strnicmp](../../../../../changes/045-strnicmp/) |
| `_strnset` | 5.72x | [changes/078-strnset](../../../../../changes/078-strnset/) |
| `_strrev` | 9.89x | [changes/070-strrev](../../../../../changes/070-strrev/) |
| `_strset` | 4.58x | [changes/077-strset](../../../../../changes/077-strset/) |
| `_strupr` | 8.94x | [changes/048-strupr](../../../../../changes/048-strupr/) |
| `_ui64toa` | 1.56x | [changes/055-ui64toa](../../../../../changes/055-ui64toa/) |
| `_ui64tow` | 1.60x | [changes/073-ui64tow](../../../../../changes/073-ui64tow/) |
| `_ultoa` | 1.50x | [changes/054-ultoa](../../../../../changes/054-ultoa/) |
| `_ultow` | 1.55x | [changes/072-ultow](../../../../../changes/072-ultow/) |
| `_wcsicmp` | 7.66x | [changes/042-wcsicmp](../../../../../changes/042-wcsicmp/) |
| `_wcsnicmp` | 7.44x | [changes/044-wcsnicmp](../../../../../changes/044-wcsnicmp/) |
| `_wcsnset` | 6.45x | [changes/080-wcsnset](../../../../../changes/080-wcsnset/) |
| `_wcsrev` | 6.60x | [changes/071-wcsrev](../../../../../changes/071-wcsrev/) |
| `_wcsset` | 3.68x | [changes/079-wcsset](../../../../../changes/079-wcsset/) |
| `_wcsupr` | 5.98x | [changes/050-wcsupr](../../../../../changes/050-wcsupr/) |
| `memchr` | 2.28x | [changes/002-memchr](../../../../../changes/002-memchr/) |
| `strcmp` | 1.31x | [changes/033-strcmp](../../../../../changes/033-strcmp/) |
| `strcspn` | 5.85x | [changes/040-strcspn](../../../../../changes/040-strcspn/) |
| `strlen` | 2.83x | [changes/032-strlen](../../../../../changes/032-strlen/) |
| `strpbrk` | 4.98x | [changes/038-strpbrk](../../../../../changes/038-strpbrk/) |
| `strspn` | 6.98x | [changes/039-strspn](../../../../../changes/039-strspn/) |
| `wcschr` | 2.19x | [changes/003-wcschr](../../../../../changes/003-wcschr/) |
| `wcscmp` | 2.88x | [changes/004-wcscmp](../../../../../changes/004-wcscmp/) |
| `wcscspn` | 8.61x | [changes/037-wcscspn](../../../../../changes/037-wcscspn/) |
| `wcslen` | 2.15x | [changes/001-wcslen](../../../../../changes/001-wcslen/) |
| `wcsncmp` | 3.33x | [changes/041-wcsncmp](../../../../../changes/041-wcsncmp/) |
| `wcspbrk` | 7.98x | [changes/035-wcspbrk](../../../../../changes/035-wcspbrk/) |
| `wcsspn` | 8.64x | [changes/036-wcsspn](../../../../../changes/036-wcsspn/) |
