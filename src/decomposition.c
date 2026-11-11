#include "nbody.h"

/*
 * Contiguous block decomposition (docs/INTERFACE.md §3).
 *
 * Part `part` of `parts` receives the half-open range [begin, end) where
 *   base  = n / parts
 *   extra = n % parts
 *   begin = part * base + min(part, extra)
 *   end   = begin + base + (part < extra ? 1 : 0)
 *
 * The first `extra` parts get one extra particle, and the union of all parts
 * is exactly [0, n). Callers guarantee n >= 0, parts >= 1, 0 <= part < parts.
 * Empty ranges (begin == end) are valid when parts > n.
 *
 * Tested by tests/test_hybrid.c (`make test-hybrid`).
 */
void partition_range(int n, int parts, int part, int *begin, int *end)
{
    const int base = n / parts;
    const int extra = n % parts;
    *begin = part * base + (part < extra ? part : extra);
    *end = *begin + base + (part < extra ? 1 : 0);
}
