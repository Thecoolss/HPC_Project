#include "nbody.h"

void particles_kick(Particles *p, int begin, int end, double half_dt)
{
    for (int i = begin; i < end; ++i) {
        p->vx[i] += half_dt * p->ax[i];
        p->vy[i] += half_dt * p->ay[i];
        p->vz[i] += half_dt * p->az[i];
    }
}

void particles_drift(Particles *p, int begin, int end, double dt)
{
    for (int i = begin; i < end; ++i) {
        p->x[i] += dt * p->vx[i];
        p->y[i] += dt * p->vy[i];
        p->z[i] += dt * p->vz[i];
    }
}
