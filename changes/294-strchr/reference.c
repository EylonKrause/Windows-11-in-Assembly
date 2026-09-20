// changes/294-strchr/reference.c: correctness oracle.
//
// Deliberately naive. Written from what probes/contract.c PROVED about the live
// ucrtbase!strchr on this machine, not from the C standard:
//
//   * only the LOW 8 BITS of the int needle are used, unsigned. Probed:
//       needle 0x1E9 finds the 0xE9 byte; 0x161 finds 'a'; 0x100 and 0xFFFFFF00
//       both find the TERMINATOR; -1 looks for the 0xFF byte.
//   * needle 0 returns a pointer to the terminator, not NULL.
//   * a needle occurring only after the terminator is not found.
//
// msvcrt!strchr was probed alongside and agreed on all 108 probes.

char* ref_strchr(const char* s, int c) {
    unsigned char needle = (unsigned char)(c & 0xFF);
    for (;;) {
        if ((unsigned char)*s == needle) return (char*)s;   // needle==0 -> the terminator
        if (*s == 0) return 0;
        ++s;
    }
}
