/* changes/275-inet-ntoa/tls.c
 *
 * THE PER-THREAD BUFFER, WHICH IS THE WHOLE CONTRACT.
 *
 * probes/contract.c measured what inet_ntoa returns a pointer to: the SAME address on every call
 * from one thread, a DIFFERENT one in another thread, and undisturbed by other Winsock calls
 * (inet_addr, htonl, WSAGetLastError and inet_ntop all leave it alone). The caller owns nothing and
 * frees nothing; the text is valid until that thread calls inet_ntoa again.
 *
 * That is ordinary thread-local storage, and unlike change 274's BSTR allocator -- which is private
 * to oleaut32 and terminates the process if a foreign block reaches it -- there is nothing here an
 * implementation cannot have its own of. It is the reason this change exists and that one is parked.
 *
 * THE ACCESS IS THE COMPILER'S, DELIBERATELY. On x64 a `__declspec(thread)` read is a load from
 * gs:[0x58], an index into the TLS array and a fixed offset -- four instructions the compiler emits
 * with the right relocations. Hand-writing that in MASM means hand-writing a SECREL32 relocation
 * against a `.tls` section, which is real work with a real chance of being subtly wrong and would
 * save a call: probes/floor.c timed the whole access at 0.38 ns against the export's 7.47. It is the
 * same decision change 269 made about LocalAlloc and change 272 about MultiByteToWideChar.
 *
 * THE BUFFER IS 32 BYTES FOR A 16-BYTE ANSWER. The formatter writes a four-byte dword per field and
 * then steps by two, three or four, so the last store can begin at offset 12 and end at 16 -- and
 * "255.255.255.255" plus a terminator is exactly 16. The slack is there so that store can never be
 * the thing that decides whether the buffer is big enough.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static __declspec(thread) char wia_ntoa_tls[32];

char* wia_ntoa_buf(void)
{
    return wia_ntoa_tls;
}
