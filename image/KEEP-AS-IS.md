# Keep-as-is — functions already at the ISA optimum (leave the shipped binary)

Per the project rule "if a function is already maximally optimized, keep it as-is; otherwise superoptimize
it." These shipped exports were disassembled and found already optimal (or within measurement noise of it),
so the image keeps Windows' own code for them. Recorded so we don't waste effort re-doing them.

## ucrtbase.dll / msvcrt.dll (already vectorized in current Win11)
| export | shipped implementation | why keep |
|---|---|---|
| `memset` | thunk → tuned SWAR + jump table | already optimal |
| `strstr` / `wcsstr` | SSE4.2 `pcmpistri` | CPU string-compare, ~23–24 GB/s |
| `strrchr` / `wcsrchr` | SSE4.2 `pcmpistri` | already vectorized |
| `wcscpy` / `wcsncpy` | AVX, feature-dispatched | already vectorized |
| `wcsnlen` | AVX, feature-dispatched | already vectorized |
| `strcpy` / `strcat` | SWAR (hasless, 8 B/iter) | decent; AVX gain marginal, dispatch-floor risk |
| `strncmp` | SSE2, ~12.8 GB/s | already vectorized |

## ntdll.dll (already optimal)
| export | shipped implementation | why keep |
|---|---|---|
| `RtlComputeCrc32` | VPCLMULQDQ, ~18 GB/s | already carry-less-mul folded; ties our best |
| internal `wcslen` (used by `RtlInitUnicodeString`) | AVX `vpcmpeqw` | already vectorized |
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
