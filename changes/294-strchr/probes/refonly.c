// changes/294-strchr/probes/refonly.c
// Step 4 of the procedure: run the change's FULL corpus with the ORACLE standing in for the
// assembly, so the reference is proven against the live export before a line of asm exists.
// A reference that disagrees with the live export means the contract is wrong, and finding
// that out after writing the assembly wastes the assembly.
char* ref_strchr(const char* s, int c);
char* wia_strchr(const char* s, int c) { return ref_strchr(s, c); }
