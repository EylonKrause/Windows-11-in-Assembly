/* changes/268-rtlunicodestringtoutf8string/heapalloc.c
 *
 * THE ONE ALLOCATION THE ALLOCATE-DESTINATION PATH NEEDS.
 *
 * This project does not reimplement allocators, and it does not have to: probes/alloc.c established
 * that RtlUnicodeStringToUTF8String with AllocateDestinationString = TRUE returns an ordinary
 * PROCESS-HEAP block of exactly (bytes + terminator), and that RtlFreeUTF8String accepts a block
 * allocated the same way by hand. So the implementation makes the SAME call rather than imitating
 * what it does -- which is the only reason this export is convertible at all.
 *
 * IT IS A C FUNCTION RATHER THAN AN ASSEMBLY IMPORT for a specific reason: RtlProcessHeap is not an
 * exported symbol -- it is a macro over the PEB -- so assembly would have to read the PEB directly
 * or link ntdll.lib, and both are a worse trade than one call through the documented API for a path
 * that is not the performance case. HeapAlloc on GetProcessHeap() is RtlAllocateHeap on the process
 * heap.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void* wia_heap_alloc(SIZE_T n)
{
    return HeapAlloc(GetProcessHeap(), 0, n);
}
