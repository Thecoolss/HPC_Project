/* OpenACC kernel tests: compute_accelerations_openacc against the serial
 * reference, called OUTSIDE any data region (the present-or-copy path), on
 * the full range, a sub-range and an empty range. The in-region path is
 * covered by running nbody_openacc --output against nbody_serial. */
#include "nbody.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

static void expect(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        ++failures;
    }
}

/* max |a - ref| / max |ref| over [begin, end), as in the Day 1 smoke test. */
static double scaled_error(const Particles *a, const Particles *ref,
                           int begin, int end)
{
    double max_ref = 0.0, max_diff = 0.0;
    for (int i = begin; i < end; ++i) {
        const double r[3] = {ref->ax[i], ref->ay[i], ref->az[i]};
        const double v[3] = {a->ax[i], a->ay[i], a->az[i]};
        for (int k = 0; k < 3; ++k) {
            if (fabs(r[k]) > max_ref) max_ref = fabs(r[k]);
            if (fabs(v[k] - r[k]) > max_diff) max_diff = fabs(v[k] - r[k]);
        }
    }
    return max_diff / (max_ref + 1e-300);
}

int main(void)
{
    const int n = 2048;
    const double G = 1.0, eps = 0.01, sentinel = -12345.0;
    Particles ref = {0}, acc = {0};
    if (!particles_alloc(&ref, n) || !particles_alloc(&acc, n) ||
        !particles_init_cube(&ref, 42) || !particles_init_cube(&acc, 42)) {
        fprintf(stderr, "setup failed\n");
        return EXIT_FAILURE;
    }

    compute_accelerations_serial(&ref, 0, n, G, eps);
    compute_accelerations_openacc(&acc, 0, n, G, eps);
    expect(scaled_error(&acc, &ref, 0, n) < 1e-12, "full range matches serial");

    /* Sub-range: only [begin, end) may be written. */
    const int begin = 100, end = 1300;
    for (int i = 0; i < n; ++i) {
        acc.ax[i] = acc.ay[i] = acc.az[i] = sentinel;
    }
    compute_accelerations_openacc(&acc, begin, end, G, eps);
    int untouched = 1;
    for (int i = 0; i < n; ++i) {
        if ((i < begin || i >= end) &&
            (acc.ax[i] != sentinel || acc.ay[i] != sentinel ||
             acc.az[i] != sentinel)) {
            untouched = 0;
        }
    }
    expect(scaled_error(&acc, &ref, begin, end) < 1e-12,
           "sub-range matches serial");
    expect(untouched, "targets outside the range are not written");

    /* Empty range is a no-op. */
    compute_accelerations_openacc(&acc, 7, 7, G, eps);
    expect(acc.ax[7] == sentinel, "empty range writes nothing");

    particles_free(&ref);
    particles_free(&acc);
    printf("%s\n", failures == 0 ? "ALL PASS" : "SOME TESTS FAILED");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
