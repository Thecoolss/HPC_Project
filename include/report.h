#ifndef NBODY_REPORT_H
#define NBODY_REPORT_H

#include "nbody.h"

#include <stdio.h>

/* Execution layout and timings of one run. All times are wall-clock seconds;
 * distributed backends report the maximum over ranks. */
typedef struct {
    const char *backend; /* "serial", "openmp", "hybrid" or "openacc" */
    int mpi_ranks;
    int omp_threads;
    int nodes;
    const char *gpu_model; /* NULL when no GPU is used */
    double elapsed_seconds;     /* initial force evaluation + all steps */
    double loop_seconds;        /* all steps, without the initial force */
    double force_seconds;       /* includes the initial force evaluation */
    double communication_seconds;
    double integration_seconds;
} RunReport;

/* Conservation diagnostics for --check runs. */
typedef struct {
    double initial_energy;
    double final_energy;
    double initial_momentum[3];
    double final_momentum[3];
} CheckReport;

/* Prints one line of whitespace-separated key=value fields. Pass NULL for
 * `check` when diagnostics were not requested. */
void report_print(FILE *out, const SimulationConfig *config,
                  const RunReport *run, const CheckReport *check);

/* Writes the final state as text: a '#' header line followed by one line per
 * particle with "x y z vx vy vz" in %.17g. Returns nonzero on success. */
int report_write_state(const char *path, const SimulationConfig *config,
                       const char *backend, const Particles *p);

#endif
