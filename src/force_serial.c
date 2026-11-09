#include "nbody.h"

#include <math.h>

void compute_accelerations_serial(Particles *p, int begin, int end,
                                  double G, double epsilon)
{
    const double epsilon_squared = epsilon * epsilon;

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
