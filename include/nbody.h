#ifndef NBODY_H
#define NBODY_H

#include <stdint.h>

/* Structure of Arrays particle storage. Every array has length n. */
typedef struct {
    int n;
    double *x, *y, *z;
    double *vx, *vy, *vz;
    double *ax, *ay, *az;
    double *mass;
} Particles;

typedef struct {
    int n, steps;
    double dt, epsilon, G;
    uint64_t seed;
} SimulationConfig;

/* Shared foundation (src/particles.c, src/integrator.c, src/diagnostics.c). */
int particles_alloc(Particles *p, int n);
void particles_free(Particles *p);
int particles_init_cube(Particles *p, uint64_t seed);

void particles_kick(Particles *p, int begin, int end, double half_dt);
void particles_drift(Particles *p, int begin, int end, double dt);

double compute_total_energy(const Particles *p, double G, double epsilon);
void compute_total_momentum(const Particles *p, double out[3]);

/*
 * Force kernels. All variants have identical semantics: read every position
 * and mass, overwrite ax/ay/az only for targets in [begin, end), and never
 * modify positions, velocities, or masses.
 */
void compute_accelerations_serial(Particles *p, int begin, int end,
                                  double G, double epsilon);
/* src/force_openmp.c (OpenMP owner); also used inside each hybrid MPI rank. */
void compute_accelerations_openmp(Particles *p, int begin, int end,
                                  double G, double epsilon);
/* src/force_openacc.c (OpenACC owner); correct whether or not the arrays are
 * already present on the device. */
void compute_accelerations_openacc(Particles *p, int begin, int end,
                                   double G, double epsilon);

/*
 * Contiguous block decomposition (src/decomposition.c, MPI owner).
 * Part `part` of `parts` receives [begin, end) where
 *   base = n / parts, extra = n % parts,
 *   begin = part * base + min(part, extra),
 *   end   = begin + base + (part < extra ? 1 : 0).
 */
void partition_range(int n, int parts, int part, int *begin, int *end);

#endif
