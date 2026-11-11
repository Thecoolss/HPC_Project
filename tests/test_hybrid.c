/*
 * Unit tests for partition_range (src/decomposition.c). No MPI needed, so
 * this runs on a laptop: `make test-hybrid`.
 *
 * The end-to-end check (hybrid with 1..P ranks against serial) is
 * tests/test_hybrid.sh, which needs mpirun/srun.
 */
#include "nbody.h"

#include <assert.h>
#include <stdio.h>

/* Every part is contiguous, parts follow each other in order, the union is
 * exactly [0, n), and sizes differ by at most one (bigger parts first). */
static void check_partition(int n, int parts)
{
    int expected_begin = 0;
    for (int part = 0; part < parts; ++part) {
        int begin = -1;
        int end = -1;
        partition_range(n, parts, part, &begin, &end);

        assert(begin == expected_begin);
        assert(begin <= end);
        assert(end <= n);

        const int size = end - begin;
        const int base = n / parts;
        assert(size == base + (part < n % parts ? 1 : 0));

        expected_begin = end;
    }
    assert(expected_begin == n);
}

/* Hand-checked values from the formula in include/nbody.h. */
static void test_known_values(void)
{
    int begin;
    int end;

    /* n = 10, 3 parts -> sizes 4, 3, 3. */
    partition_range(10, 3, 0, &begin, &end);
    assert(begin == 0 && end == 4);
    partition_range(10, 3, 1, &begin, &end);
    assert(begin == 4 && end == 7);
    partition_range(10, 3, 2, &begin, &end);
    assert(begin == 7 && end == 10);

    /* One part owns everything. */
    partition_range(1000, 1, 0, &begin, &end);
    assert(begin == 0 && end == 1000);
}

/* More parts than particles: the last parts are empty, which is valid. */
static void test_more_parts_than_particles(void)
{
    int begin;
    int end;
    partition_range(3, 5, 4, &begin, &end);
    assert(begin == 3 && end == 3);
    check_partition(3, 5);
    check_partition(0, 4);
}

static void test_many_shapes(void)
{
    static const int sizes[] = {1, 2, 7, 37, 1000, 1001, 100000};
    static const int part_counts[] = {1, 2, 3, 4, 7, 8, 16, 32, 64};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) {
        for (size_t j = 0; j < sizeof part_counts / sizeof part_counts[0];
             ++j) {
            check_partition(sizes[i], part_counts[j]);
        }
    }
}

int main(void)
{
    test_known_values();
    test_more_parts_than_particles();
    test_many_shapes();
    printf("test_hybrid: all partition_range tests passed\n");
    return 0;
}
