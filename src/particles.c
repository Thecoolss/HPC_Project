#include "nbody.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static uint64_t splitmix64_next(uint64_t *state)
{
    uint64_t value = (*state += UINT64_C(0x9e3779b97f4a7c15));
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

static double random_unit_interval(uint64_t *state)
{
    return (double)(splitmix64_next(state) >> 11) * 0x1.0p-53;
}

int particles_alloc(Particles *p, int n)
{
    if (p == NULL) {
        return 0;
    }
    *p = (Particles){0};
    if (n < 1 || (size_t)n > SIZE_MAX / sizeof(double)) {
        return 0;
    }

    Particles allocated = {0};
    allocated.n = n;
    allocated.x = calloc((size_t)n, sizeof(double));
    allocated.y = calloc((size_t)n, sizeof(double));
    allocated.z = calloc((size_t)n, sizeof(double));
    allocated.vx = calloc((size_t)n, sizeof(double));
    allocated.vy = calloc((size_t)n, sizeof(double));
    allocated.vz = calloc((size_t)n, sizeof(double));
    allocated.ax = calloc((size_t)n, sizeof(double));
    allocated.ay = calloc((size_t)n, sizeof(double));
    allocated.az = calloc((size_t)n, sizeof(double));
    allocated.mass = calloc((size_t)n, sizeof(double));

    if (allocated.x == NULL || allocated.y == NULL || allocated.z == NULL ||
        allocated.vx == NULL || allocated.vy == NULL || allocated.vz == NULL ||
        allocated.ax == NULL || allocated.ay == NULL || allocated.az == NULL ||
        allocated.mass == NULL) {
        particles_free(&allocated);
        *p = (Particles){0};
        return 0;
    }

    *p = allocated;
    return 1;
}

void particles_free(Particles *p)
{
    if (p == NULL) {
        return;
    }

    free(p->x);
    free(p->y);
    free(p->z);
    free(p->vx);
    free(p->vy);
    free(p->vz);
    free(p->ax);
    free(p->ay);
    free(p->az);
    free(p->mass);
    *p = (Particles){0};
}

int particles_init_cube(Particles *p, uint64_t seed)
{
    if (p == NULL || p->n < 1 || p->x == NULL || p->y == NULL ||
        p->z == NULL || p->vx == NULL || p->vy == NULL || p->vz == NULL ||
        p->ax == NULL || p->ay == NULL || p->az == NULL || p->mass == NULL) {
        return 0;
    }

    uint64_t state = seed;
    double mass_sum = 0.0;
    for (int i = 0; i < p->n; ++i) {
        p->x[i] = 2.0 * random_unit_interval(&state) - 1.0;
        p->y[i] = 2.0 * random_unit_interval(&state) - 1.0;
        p->z[i] = 2.0 * random_unit_interval(&state) - 1.0;
        p->mass[i] = 0.5 + random_unit_interval(&state);
        p->vx[i] = 0.0;
        p->vy[i] = 0.0;
        p->vz[i] = 0.0;
        p->ax[i] = 0.0;
        p->ay[i] = 0.0;
        p->az[i] = 0.0;
        mass_sum += p->mass[i];
    }

    for (int i = 0; i < p->n; ++i) {
        p->mass[i] /= mass_sum;
    }

    return 1;
}
