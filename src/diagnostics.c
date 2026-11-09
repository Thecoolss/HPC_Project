#include "nbody.h"

#include <math.h>

double compute_total_energy(const Particles *p, double G, double epsilon)
{
    double energy = 0.0;
    const double epsilon_squared = epsilon * epsilon;

    for (int i = 0; i < p->n; ++i) {
        energy += 0.5 * p->mass[i] *
                  (p->vx[i] * p->vx[i] + p->vy[i] * p->vy[i] +
                   p->vz[i] * p->vz[i]);
        for (int j = i + 1; j < p->n; ++j) {
            const double dx = p->x[j] - p->x[i];
            const double dy = p->y[j] - p->y[i];
            const double dz = p->z[j] - p->z[i];
            const double distance =
                sqrt(dx * dx + dy * dy + dz * dz + epsilon_squared);
            energy -= G * p->mass[i] * p->mass[j] / distance;
        }
    }

    return energy;
}

void compute_total_momentum(const Particles *p, double out[3])
{
    out[0] = 0.0;
    out[1] = 0.0;
    out[2] = 0.0;

    for (int i = 0; i < p->n; ++i) {
        out[0] += p->mass[i] * p->vx[i];
        out[1] += p->mass[i] * p->vy[i];
        out[2] += p->mass[i] * p->vz[i];
    }
}
