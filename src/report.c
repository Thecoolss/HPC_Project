#define _POSIX_C_SOURCE 200809L

#include "report.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Build metadata injected by the Makefile. */
#ifndef NBODY_COMPILER
#define NBODY_COMPILER "unknown"
#endif
#ifndef NBODY_BUILD_FLAGS
#define NBODY_BUILD_FLAGS "unknown"
#endif
#ifndef NBODY_GIT_COMMIT
#define NBODY_GIT_COMMIT "unavailable"
#endif
#ifdef __VERSION__
#define NBODY_COMPILER_VERSION __VERSION__
#else
#define NBODY_COMPILER_VERSION "unknown"
#endif

static const char *env_or(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return (value != NULL && value[0] != '\0') ? value : fallback;
}

void report_print(FILE *out, const SimulationConfig *config,
                  const RunReport *run, const CheckReport *check)
{
    char host[256] = "unknown";
    if (gethostname(host, sizeof host) != 0) {
        snprintf(host, sizeof host, "unknown");
    }
    host[sizeof host - 1] = '\0';

    /* Pair interactions per step: n * (n - 1), computed in double. */
    const double interactions =
        (double)config->n * (double)(config->n - 1) * (double)config->steps;
    const double seconds_per_step = run->loop_seconds / config->steps;
    const double interactions_per_second =
        run->loop_seconds > 0.0 ? interactions / run->loop_seconds : 0.0;

    fprintf(out,
            "backend=%s n=%d steps=%d G=%.17g dt=%.17g epsilon=%.17g "
            "seed=%llu mpi_ranks=%d omp_threads=%d nodes=%d gpu_model=\"%s\" "
            "host=%s slurm_job_id=%s git_commit=%s compiler=%s "
            "compiler_version=\"%s\" build_flags=\"%s\" "
            "elapsed_seconds=%.9f loop_seconds=%.9f seconds_per_step=%.9f "
            "interactions_per_second=%.6e force_seconds=%.9f "
            "communication_seconds=%.9f integration_seconds=%.9f",
            run->backend, config->n, config->steps, config->G, config->dt,
            config->epsilon, (unsigned long long)config->seed,
            run->mpi_ranks, run->omp_threads, run->nodes,
            run->gpu_model != NULL ? run->gpu_model : "none", host,
            env_or("SLURM_JOB_ID", "none"), NBODY_GIT_COMMIT, NBODY_COMPILER,
            NBODY_COMPILER_VERSION, NBODY_BUILD_FLAGS, run->elapsed_seconds,
            run->loop_seconds, seconds_per_step, interactions_per_second,
            run->force_seconds, run->communication_seconds,
            run->integration_seconds);

    if (check != NULL) {
        const double energy_change =
            check->final_energy - check->initial_energy;
        fprintf(out,
                " initial_energy=%.17g final_energy=%.17g "
                "energy_change=%.17g relative_energy_change=%.17g "
                "initial_momentum_x=%.17g initial_momentum_y=%.17g "
                "initial_momentum_z=%.17g momentum_x=%.17g momentum_y=%.17g "
                "momentum_z=%.17g",
                check->initial_energy, check->final_energy, energy_change,
                energy_change / check->initial_energy,
                check->initial_momentum[0], check->initial_momentum[1],
                check->initial_momentum[2], check->final_momentum[0],
                check->final_momentum[1], check->final_momentum[2]);
    }
    fputc('\n', out);
}

int report_write_state(const char *path, const SimulationConfig *config,
                       const char *backend, const Particles *p)
{
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return 0;
    }

    fprintf(file,
            "# nbody_state backend=%s n=%d steps=%d dt=%.17g epsilon=%.17g "
            "seed=%llu git_commit=%s columns=x,y,z,vx,vy,vz\n",
            backend, p->n, config->steps, config->dt, config->epsilon,
            (unsigned long long)config->seed, NBODY_GIT_COMMIT);
    for (int i = 0; i < p->n; ++i) {
        fprintf(file, "%.17g %.17g %.17g %.17g %.17g %.17g\n", p->x[i],
                p->y[i], p->z[i], p->vx[i], p->vy[i], p->vz[i]);
    }

    const int write_failed = ferror(file);
    const int close_failed = fclose(file) != 0;
    return !write_failed && !close_failed;
}
