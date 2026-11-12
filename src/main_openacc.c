#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include "nbody.h"
#include "report.h"

#include <openacc.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Monotonic wall-clock time in seconds. OpenACC compute regions without
 * `async` are synchronous, so host timers bracket complete kernels. */
static double now_seconds(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (double)time.tv_sec + (double)time.tv_nsec * 1.0e-9;
}

/* Device versions of particles_kick / particles_drift (src/integrator.c is
 * host-only). `present` makes a missing data region a hard error instead of
 * a silent per-step transfer. */
static void kick_device(Particles *p, double half_dt)
{
    const int n = p->n;
    double *restrict vx = p->vx, *restrict vy = p->vy, *restrict vz = p->vz;
    const double *restrict ax = p->ax, *restrict ay = p->ay,
                 *restrict az = p->az;

    #pragma acc parallel loop present(vx[0:n], vy[0:n], vz[0:n], \
                                      ax[0:n], ay[0:n], az[0:n])
    for (int i = 0; i < n; ++i) {
        vx[i] += half_dt * ax[i];
        vy[i] += half_dt * ay[i];
        vz[i] += half_dt * az[i];
    }
}

static void drift_device(Particles *p, double dt)
{
    const int n = p->n;
    double *restrict x = p->x, *restrict y = p->y, *restrict z = p->z;
    const double *restrict vx = p->vx, *restrict vy = p->vy,
                 *restrict vz = p->vz;

    #pragma acc parallel loop present(x[0:n], y[0:n], z[0:n], \
                                      vx[0:n], vy[0:n], vz[0:n])
    for (int i = 0; i < n; ++i) {
        x[i] += dt * vx[i];
        y[i] += dt * vy[i];
        z[i] += dt * vz[i];
    }
}

/* GPU name for the result line; "none" when running the host fallback
 * (e.g. a gcc -fopenacc build without offloading). */
static const char *gpu_model_name(void)
{
    if (acc_get_num_devices(acc_device_nvidia) < 1) {
        return NULL;
    }
    const char *name = acc_get_property_string(
        acc_get_device_num(acc_device_nvidia), acc_device_nvidia,
        acc_property_name);
    return name != NULL ? name : "nvidia (unknown model)";
}

int main(int argc, char **argv)
{
    SimulationConfig config;
    RunOptions options;
    const ConfigStatus status = config_parse(argc, argv, &config, &options);
    if (status == CONFIG_HELP) {
        return EXIT_SUCCESS;
    }
    if (status == CONFIG_INVALID) {
        config_print_usage(argv[0]);
        fprintf(stderr, "Invalid command-line arguments\n");
        return EXIT_FAILURE;
    }

    struct timespec probe;
    if (clock_gettime(CLOCK_MONOTONIC, &probe) != 0) {
        fprintf(stderr, "Monotonic clock unavailable\n");
        return EXIT_FAILURE;
    }

    Particles particles = {0};
    if (!particles_alloc(&particles, config.n)) {
        fprintf(stderr, "Failed to allocate particle arrays\n");
        return EXIT_FAILURE;
    }
    if (!particles_init_cube(&particles, config.seed)) {
        fprintf(stderr, "Failed to initialize particles\n");
        particles_free(&particles);
        return EXIT_FAILURE;
    }

    /* Initialize the device runtime outside the timers, so first-touch
     * context creation is not counted as transfer time. */
    acc_init(acc_get_device_type());
    const char *gpu_model = gpu_model_name();

    CheckReport check = {0};
    if (options.check) {
        check.initial_energy =
            compute_total_energy(&particles, config.G, config.epsilon);
        compute_total_momentum(&particles, check.initial_momentum);
    }

    const int n = particles.n;
    const double half_dt = 0.5 * config.dt;
    double force_seconds = 0.0;
    double integration_seconds = 0.0;
    double communication_seconds = 0.0;
    double t0;

    Particles *p = &particles;
    double *x = p->x, *y = p->y, *z = p->z;
    double *vx = p->vx, *vy = p->vy, *vz = p->vz;
    double *ax = p->ax, *ay = p->ay, *az = p->az;
    double *mass = p->mass;
    /* Used only in data clauses; GCC's -Wunused-variable does not see those. */
    (void)x; (void)y; (void)z; (void)vx; (void)vy; (void)vz;
    (void)ax; (void)ay; (void)az; (void)mass;

    /* End-to-end timer: host->device copy, initial force, all steps and the
     * final device->host copy. Transfers go in communication_seconds. */
    const double total_start = now_seconds();

    /* One device data region for the whole run: arrays stay on the GPU
     * between steps, nothing crosses PCIe inside the loop. */
    #pragma acc enter data copyin(x[0:n], y[0:n], z[0:n], vx[0:n], vy[0:n], \
                                  vz[0:n], mass[0:n]) \
                           create(ax[0:n], ay[0:n], az[0:n])
    const double copyin_end = now_seconds();
    communication_seconds += copyin_end - total_start;

    compute_accelerations_openacc(p, 0, n, config.G, config.epsilon);
    const double loop_start = now_seconds();
    force_seconds += loop_start - copyin_end;

    for (int step = 0; step < config.steps; ++step) {
        t0 = now_seconds();
        kick_device(p, half_dt);
        drift_device(p, config.dt);
        integration_seconds += now_seconds() - t0;

        t0 = now_seconds();
        compute_accelerations_openacc(p, 0, n, config.G, config.epsilon);
        force_seconds += now_seconds() - t0;

        t0 = now_seconds();
        kick_device(p, half_dt);
        integration_seconds += now_seconds() - t0;
    }
    const double loop_end = now_seconds();

    #pragma acc exit data copyout(x[0:n], y[0:n], z[0:n], vx[0:n], vy[0:n], \
                                  vz[0:n], ax[0:n], ay[0:n], az[0:n]) \
                          delete(mass[0:n])
    const double total_end = now_seconds();
    communication_seconds += total_end - loop_end;

    const RunReport run = {
        .backend = "openacc",
        .mpi_ranks = 1,
        .omp_threads = 1,
        .nodes = 1,
        .gpu_model = gpu_model,
        .elapsed_seconds = total_end - total_start,
        .loop_seconds = loop_end - loop_start,
        .force_seconds = force_seconds,
        .communication_seconds = communication_seconds,
        .integration_seconds = integration_seconds
    };

    if (options.check) {
        check.final_energy =
            compute_total_energy(&particles, config.G, config.epsilon);
        compute_total_momentum(&particles, check.final_momentum);
    }
    report_print(stdout, &config, &run, options.check ? &check : NULL);

    int exit_code = EXIT_SUCCESS;
    if (options.output_path != NULL &&
        !report_write_state(options.output_path, &config, run.backend,
                            &particles)) {
        fprintf(stderr, "Failed to write final state to %s\n",
                options.output_path);
        exit_code = EXIT_FAILURE;
    }

    particles_free(&particles);
    return exit_code;
}
