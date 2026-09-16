/* changes/275-inet-ntoa/reference.c
 *
 * The scalar model for ws2_32!inet_ntoa, written to be obviously right rather than fast.
 *
 * probes/contract.c established that the text is "%u.%u.%u.%u" of the four address bytes in memory
 * order, over every one of the 256 byte values in each of the four positions -- all 1024
 * combinations, zero disagreements. So the model divides.
 *
 * It writes into a buffer the CALLER provides, deliberately. The export's contract is a per-thread
 * buffer of its own, and a model with a third one would add nothing: what the gate needs a second
 * opinion about is the TEXT, not the storage.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

char* ref_inet_ntoa(unsigned long net, char* out)
{
    const unsigned char* b = (const unsigned char*)&net;
    char* p = out;
    int i;
    for (i = 0; i < 4; ++i) {
        unsigned v = b[i];
        if (v >= 100) *p++ = (char)('0' + v / 100);
        if (v >= 10)  *p++ = (char)('0' + (v / 10) % 10);
        *p++ = (char)('0' + v % 10);
        if (i < 3) *p++ = '.';
    }
    *p = 0;
    return out;
}
