#include "nbody.h"

#include <math.h>

/*
 * SIMD variant of compute_accelerations_openmp, built as its own target
 * (nbody_openmp_simd) so the scalar version stays reproducible.
 *
 * Changes from the scalar kernel:
 *  - the j == i branch is removed. For j == i, dx = dy = dz = 0, so the
 *    added term is exactly 0; the softening keeps the denominator finite
 *    because epsilon > 0 is enforced by the CLI.
 *  - the j loop is vectorized with `omp simd reduction`, so the three
 *    accumulators are summed in lanes. This changes the summation order, so
 *    results agree with serial to rounding error, not bitwise.
 */
void compute_accelerations_openmp(Particles *p, int begin, int end,
                                  double G, double epsilon)
{
    const double epsilon_squared = epsilon * epsilon;
    const double *restrict x = p->x;
    const double *restrict y = p->y;
    const double *restrict z = p->z;
    const double *restrict mass = p->mass;
    const int n = p->n;

#pragma omp parallel for schedule(static)
    for (int i = begin; i < end; ++i) {
        const double xi = x[i];
        const double yi = y[i];
        const double zi = z[i];
        double ax = 0.0;
        double ay = 0.0;
        double az = 0.0;

#pragma omp simd reduction(+ : ax, ay, az)
        for (int j = 0; j < n; ++j) {
            const double dx = x[j] - xi;
            const double dy = y[j] - yi;
            const double dz = z[j] - zi;
            const double distance_squared =
                dx * dx + dy * dy + dz * dz + epsilon_squared;
            const double inverse_distance_cubed =
                1.0 / (distance_squared * sqrt(distance_squared));
            const double scale = G * mass[j] * inverse_distance_cubed;

            ax += scale * dx;
            ay += scale * dy;
            az += scale * dz;
        }

        p->ax[i] = ax;
        p->ay[i] = ay;
        p->az[i] = az;
    }
}
