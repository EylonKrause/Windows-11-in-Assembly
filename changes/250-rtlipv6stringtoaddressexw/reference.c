/* changes/250-rtlipv6stringtoaddressexw/reference.c
 *
 * An INDEPENDENT oracle for RtlIpv6StringToAddressExW's ENVELOPE.
 *
 * What it deliberately does not model: the IPv6 address grammar. It calls the live
 * RtlIpv6StringToAddressW for that, which is the right split three times over:
 *
 *   * it is what the shipped function does; the call at RVA 0x0C318E targets 0x0C33F0, which is
 *     that export's own RVA, so the oracle and the subject are structured the same way;
 *   * the address body is change 166, which has its own four gates and its own 65536-unit sweeps,
 *     and re-deriving an eight-group hex grammar with "::" compression and an embedded IPv4 tail
 *     here would be inviting a second, differently-wrong copy of it;
 *   * it makes the oracle-versus-ours comparison isolate the envelope. If those two disagree, the
 *     bug is in the fourteen rules below and nowhere else. Anything wrong in the address body shows
 *     up instead in the ours-versus-live-export comparison, which covers both halves at once.
 *
 * THE ENVELOPE, exactly as probes/ip6exw.c measured it against the live export:
 *
 *   1.  all four arguments NULL-checked -> STATUS_INVALID_PARAMETER;
 *   2.  an optional leading '[';
 *   3.  the address, by the W parser, which also reports where it stopped;
 *   4.  on any later failure the address stays written and *ScopeId / *Port are not touched;
 *   5.  an optional "%<scope>", decimal, ASCII '0'-'9' only, swept over all 65536 units, exactly
 *       ten are accepted and none is >= 0x80;
 *   6.  a '%' with no digit after it is an error;
 *   7.  scope <= 4294967295, refused at 4294967296;
 *   8.  an optional ']', which is an error without a '[';
 *   9.  and only then an optional ':<port>', so "::1:80" is an address, not a port;
 *   10. the port's base: "0x"/"0X" -> 16, a leading '0' -> 8, otherwise 10;
 *   11. an empty port body is ZERO: "[::1]:" and "[::1]:0x" both give 0;
 *   12. port digits are ASCII only too, at every base, 10 units decimal, 22 hex;
 *   13. port <= 65535, refused at 65536, and *Port is stored in NETWORK order;
 *   14. The whole string must be consumed, and a '[' must have been closed. This is the one rule
 *       with no counterpart in the W form, which has a Terminator out-parameter instead, and it
 *       is why "::0x1" and "::1.2.3.0x5" succeed there and fail here.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#define REF_BADPARAM ((LONG)0xC000000DL)

typedef LONG (NTAPI *REF_FW)(const wchar_t*, const wchar_t**, void*);
static REF_FW ref_w;

int ref_init(void)
{
    HMODULE h = LoadLibraryW(L"ntdll.dll");
    ref_w = (REF_FW)GetProcAddress(h, "RtlIpv6StringToAddressW");
    return ref_w != 0;
}

/* ASCII only, by measurement: nothing at or above 0x80 is a digit in this envelope. */
static int ref_dig(unsigned u, int base)
{
    int v;
    if (u >= 128) return -1;
    if (u >= '0' && u <= '9') v = (int)(u - '0');
    else if ((u | 32) >= 'a' && (u | 32) <= 'f') v = (int)((u | 32) - 'a') + 10;
    else return -1;
    return (v < base) ? v : -1;
}

LONG ref_ip6exw(const wchar_t* S, void* Addr, ULONG* ScopeId, USHORT* Port)
{
    const wchar_t* p;
    const wchar_t* q = 0;
    unsigned __int64 scope = 0;
    unsigned port = 0;
    int bracket = 0, base, ndig;
    LONG st;

    if (!S || !Addr || !ScopeId || !Port) return REF_BADPARAM;
    if (!ref_w) return (LONG)0xDEAD0001L;          /* ref_init was not called: fail loudly */

    p = S;
    if (*p == L'[') { bracket = 1; ++p; }

    st = ref_w(p, &q, Addr);                       /* writes Addr even if we fail below */
    if (st < 0) return REF_BADPARAM;

    if (*q == L'%') {
        ++q;
        if (ref_dig((unsigned)*q, 10) < 0) return REF_BADPARAM;   /* '%' with no digit */
        while (ref_dig((unsigned)*q, 10) >= 0) {
            scope = scope * 10 + (unsigned)ref_dig((unsigned)*q, 10);
            if (scope > 0xFFFFFFFFull) return REF_BADPARAM;
            ++q;
        }
    }

    if (*q == L']') {
        if (!bracket) return REF_BADPARAM;
        bracket = 0;
        ++q;
        if (*q == L':') {
            ++q;
            base = 10;
            if (*q == L'0') {
                if ((q[1] | 32) == L'x') { base = 16; q += 2; }
                else                     { base = 8;  q += 1; }
            }
            ndig = 0;
            while (ref_dig((unsigned)*q, base) >= 0) {
                port = port * (unsigned)base + (unsigned)ref_dig((unsigned)*q, base);
                if (port > 65535) return REF_BADPARAM;
                ++q; ++ndig;
            }
            (void)ndig;                            /* an empty body is legal and means zero */
        }
    }

    if (bracket) return REF_BADPARAM;              /* '[' never closed */
    if (*q != 0)  return REF_BADPARAM;             /* the Ex form must consume everything */

    *ScopeId = (ULONG)scope;
    *Port    = (USHORT)(((port & 0xFF) << 8) | ((port >> 8) & 0xFF));   /* network order */
    return 0;
}
