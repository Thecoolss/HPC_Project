#include "nbody.h"

#include <math.h>

/*
 * Threaded version of compute_accelerations_serial. Each thread owns whole
 * target particles, so no two threads write the same ax/ay/az and there are
 * no shared accumulators. The per-target arithmetic and summation order match
 * the serial kernel, so the result is bit-identical for any thread count.
 */
void compute_accelerations_openmp(Particles *p, int begin, int end,
                                  double G, double epsilon)
{
    const double epsilon_squared = epsilon * epsilon;

#pragma omp parallel for schedule(static)
    for (int i = begin; i < end; ++i) {
        double ax = 0.0;
        double ay = 0.0;
        double az = 0.0;

        for (int j = 0; j < p->n; ++j) {
            if (j == i) {
                continue;
            }

            const double dx = p->x[j] - p->x[i];
            const double dy = p->y[j] - p->y[i];
            const double dz = p->z[j] - p->z[i];
            const double distance_squared =
                dx * dx + dy * dy + dz * dz + epsilon_squared;
            const double inverse_distance_cubed =
                1.0 / (distance_squared * sqrt(distance_squared));
            const double scale = G * p->mass[j] * inverse_distance_cubed;

            ax += scale * dx;
            ay += scale * dy;
            az += scale * dz;
        }

        p->ax[i] = ax;
        p->ay[i] = ay;
        p->az[i] = az;
    }
}
