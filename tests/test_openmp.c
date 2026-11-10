#include "nbody.h"

#include <assert.h>
#include <math.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define G_CONST 1.0
#define EPS 0.01

/* Two identically initialized systems: `ref` gets the serial kernel, `omp`
 * gets the OpenMP kernel under test. */
static void make_pair(Particles *ref, Particles *omp, int n, uint64_t seed)
{
    assert(particles_alloc(ref, n));
    assert(particles_alloc(omp, n));
    assert(particles_init_cube(ref, seed));
    assert(particles_init_cube(omp, seed));
}

static int close_enough(double cand, double ref)
{
    return fabs(cand - ref) <= 1e-10 + 1e-8 * fabs(ref);
}

static void assert_acc_match(const Particles *ref, const Particles *omp,
                             int begin, int end)
{
    for (int i = begin; i < end; ++i) {
        assert(close_enough(omp->ax[i], ref->ax[i]));
        assert(close_enough(omp->ay[i], ref->ay[i]));
        assert(close_enough(omp->az[i], ref->az[i]));
    }
}

/* Full range matches serial for several thread counts (1 thread first). */
static void test_full_range_matches_serial(void)
{
    static const int thread_counts[] = {1, 2, 4, 8};
    for (size_t t = 0; t < sizeof thread_counts / sizeof thread_counts[0];
         ++t) {
        Particles ref = {0};
        Particles omp = {0};
        make_pair(&ref, &omp, 37, UINT64_C(7));
        compute_accelerations_serial(&ref, 0, ref.n, G_CONST, EPS);
        omp_set_num_threads(thread_counts[t]);
        compute_accelerations_openmp(&omp, 0, omp.n, G_CONST, EPS);
        assert_acc_match(&ref, &omp, 0, ref.n);
        particles_free(&ref);
        particles_free(&omp);
    }
}

/* A sub-range writes only its own targets and leaves the rest untouched. */
static void test_subrange_writes_only_targets(void)
{
    Particles ref = {0};
    Particles omp = {0};
    make_pair(&ref, &omp, 50, UINT64_C(99));
    compute_accelerations_serial(&ref, 0, ref.n, G_CONST, EPS);

    for (int i = 0; i < omp.n; ++i) {
        omp.ax[i] = omp.ay[i] = omp.az[i] = 123.0; /* sentinel */
    }
    compute_accelerations_openmp(&omp, 5, 20, G_CONST, EPS);

    for (int i = 0; i < omp.n; ++i) {
        if (i >= 5 && i < 20) {
            assert(close_enough(omp.ax[i], ref.ax[i]));
        } else {
            assert(omp.ax[i] == 123.0 && omp.ay[i] == 123.0 &&
                   omp.az[i] == 123.0);
        }
    }
    particles_free(&ref);
    particles_free(&omp);
}

/* Empty range is a no-op; a single particle has zero acceleration. */
static void test_empty_and_single_particle(void)
{
    Particles one = {0};
    assert(particles_alloc(&one, 1));
    assert(particles_init_cube(&one, UINT64_C(3)));
    compute_accelerations_openmp(&one, 0, 1, G_CONST, EPS);
    assert(one.ax[0] == 0.0 && one.ay[0] == 0.0 && one.az[0] == 0.0);

    one.ax[0] = 5.0;
    compute_accelerations_openmp(&one, 0, 0, G_CONST, EPS);
    assert(one.ax[0] == 5.0);
    particles_free(&one);
}

/* The kernel never touches positions, velocities or masses. */
static void test_inputs_unchanged(void)
{
    Particles p = {0};
    assert(particles_alloc(&p, 20));
    assert(particles_init_cube(&p, UINT64_C(5)));
    double *snapshot = malloc(sizeof(double) * 20 * 7);
    assert(snapshot != NULL);
    memcpy(snapshot, p.x, sizeof(double) * 20);
    memcpy(snapshot + 20, p.y, sizeof(double) * 20);
    memcpy(snapshot + 40, p.z, sizeof(double) * 20);
    memcpy(snapshot + 60, p.mass, sizeof(double) * 20);
    compute_accelerations_openmp(&p, 0, 20, G_CONST, EPS);
    assert(memcmp(snapshot, p.x, sizeof(double) * 20) == 0);
    assert(memcmp(snapshot + 20, p.y, sizeof(double) * 20) == 0);
    assert(memcmp(snapshot + 40, p.z, sizeof(double) * 20) == 0);
    assert(memcmp(snapshot + 60, p.mass, sizeof(double) * 20) == 0);
    free(snapshot);
    particles_free(&p);
}

int main(void)
{
    test_full_range_matches_serial();
    test_subrange_writes_only_targets();
    test_empty_and_single_particle();
    test_inputs_unchanged();
    printf("test_openmp: all checks passed\n");
    return 0;
}
