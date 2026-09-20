# Keep-as-is — functions already at the ISA optimum (leave the shipped binary)

Per the project rule "if a function is already maximally optimized, keep it as-is; otherwise superoptimize
it." These shipped exports were disassembled and found already optimal (or within measurement noise of it),
so the image keeps Windows' own code for them. Recorded so we don't waste effort re-doing them.

**Audited 2026-09-20.** Every export named here was cross-checked against `image/tree` — a name
that is both "keep as is" and materialised is a contradiction, and one of the four hits was real:
`wcsrchr`, paired with `strrchr` on the assumption that a narrow and a wide sibling share an
implementation. They do not, and change 149 landed the wide one at 1.82×. The other three were
ambiguous wording rather than wrong claims, and are now disambiguated in place.

Re-run the check with:

```
py tools/keepasis-audit.py
```


## ucrtbase.dll / msvcrt.dll (already vectorized in current Win11)
| export | shipped implementation | why keep |
|---|---|---|
| `memset` | thunk → tuned SWAR + jump table | already optimal |
| `strstr` / `wcsstr` | SSE4.2 `pcmpistri` | CPU string-compare, ~23–24 GB/s |
| `strrchr` | SSE4.2 `pcmpistri` | already vectorized |
| ~~`wcsrchr`~~ | **CORRECTED 2026-09-20** | this row used to pair `wcsrchr` with `strrchr` and claim both were SSE4.2. Only the narrow one is. Change [149](../changes/149-wcsrchr/) measured `ucrtbase!wcsrchr` at **0.117 ns/char — scalar** (29.8 ns for 254 characters, 238 ns for 2048) while `strrchr` does the same 254 characters in 11.8 ns, and it **LANDED at 1.82×**. The wide sibling was simply left behind, exactly as `wcstok_s` was against `strtok_s` (change 148). Pairing a narrow and a wide export in one row on the assumption they share an implementation is how this got recorded wrong |
| `wcscpy` / `wcsncpy` | AVX, feature-dispatched | already vectorized |
| `wcsnlen` | AVX, feature-dispatched | already vectorized |
| `strcpy` / `strcat` | SWAR (hasless, 8 B/iter) | decent; AVX gain marginal, dispatch-floor risk |
| `strncmp` | SSE2, ~12.8 GB/s | already vectorized |

## ntdll.dll (already optimal)
| export | shipped implementation | why keep |
|---|---|---|
| `RtlComputeCrc32` | VPCLMULQDQ, ~18 GB/s | already carry-less-mul folded; ties our best |
| ntdll's *internal* wcslen, the one `RtlInitUnicodeString` calls | AVX `vpcmpeqw` | already vectorized. **This row is about ntdll's private routine, not about either export named in it**: `ucrtbase!wcslen` is change [001](../changes/001-wcslen/) and `ntdll!RtlInitUnicodeString` is change [094](../changes/094-rtlinitunicodestring/), both landed and both in the image tree. Naming two converted exports inside a row in the keep-as-is register is worth exactly one sentence of disambiguation |
| `RtlCopyMemory` / `RtlMoveMemory` | tuned size-laddered SSE | memcpy, already optimal |
| `RtlEqualUnicodeString` (cs path) | ~35 GB/s | already fast — but we still **beat** it 1.15–1.57× (landed 010) |

## Parked (correct + beat at size, but a genuine small-size dispatch floor — kept shipped)
| export | our result | why parked |
|---|---|---|
| `memcmp` (ucrtbase) | 1.8–2× ≥ 1 KB | loses ≤ 32 B; ucrtbase's tight SSE2 small path is the floor |
| `RtlComputeCrc32` (ntdll) | ties | ntdll already VPCLMULQDQ |
| `_wcslwr` (ucrtbase) | 2–6× ≥ 32 B | ucrtbase's tight 8-wchar path wins at 8 (narrowed to 0.91×) |

Anything not listed here and not already reimplemented under `tree/` is **unexplored** — a candidate for a
future conversion pass.
