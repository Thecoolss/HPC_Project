#include "nbody.h"

#include <math.h>

/*
 * OpenACC all-pairs force kernel. Same semantics as
 * compute_accelerations_serial: reads every position and mass, overwrites
 * ax/ay/az for targets in [begin, end) only.
 *
 * Data clauses are present-or-copy (OpenACC >= 2.5): inside the device data
 * region opened by main_openacc.c nothing is transferred; called on its own
 * (e.g. from a test) the arrays are copied in and the results copied out.
 *
 * One GPU thread per target particle (gang vector over i), sequential inner
 * loop over sources j. Optimizations (tiling sources through shared memory,
 * vector_length tuning) are later experiments; this is the baseline.
 */
void compute_accelerations_openacc(Particles *p, int begin, int end,
                                   double G, double epsilon)
{
    if (begin >= end) {
        return;
    }

    const int n = p->n;
    const int count = end - begin;
    const double epsilon_squared = epsilon * epsilon;
    const double *restrict x = p->x;
    const double *restrict y = p->y;
    const double *restrict z = p->z;
    const double *restrict mass = p->mass;
    double *restrict ax = p->ax;
    double *restrict ay = p->ay;
    double *restrict az = p->az;

    #pragma acc parallel loop gang vector \
        copyin(x[0:n], y[0:n], z[0:n], mass[0:n]) \
        copyout(ax[begin:count], ay[begin:count], az[begin:count])
    for (int i = begin; i < end; ++i) {
        const double xi = x[i];
        const double yi = y[i];
        const double zi = z[i];
        double sum_x = 0.0;
        double sum_y = 0.0;
        double sum_z = 0.0;

        #pragma acc loop seq
        for (int j = 0; j < n; ++j) {
            if (j == i) {
                continue;
            }
            const double dx = x[j] - xi;
            const double dy = y[j] - yi;
            const double dz = z[j] - zi;
            const double distance_squared =
                dx * dx + dy * dy + dz * dz + epsilon_squared;
            const double inverse_distance_cubed =
                1.0 / (distance_squared * sqrt(distance_squared));
            const double scale = G * mass[j] * inverse_distance_cubed;

            sum_x += scale * dx;
            sum_y += scale * dy;
            sum_z += scale * dz;
        }

        ax[i] = sum_x;
        ay[i] = sum_y;
        az[i] = sum_z;
    }
}
