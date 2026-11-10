#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include "nbody.h"
#include "report.h"

#include <omp.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Monotonic wall-clock time in seconds. */
static double now_seconds(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (double)time.tv_sec + (double)time.tv_nsec * 1.0e-9;
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
    double t0;

    /* Primary timer: initial force evaluation plus all KDK steps. */
    const double total_start = now_seconds();
    compute_accelerations_openmp(&particles, 0, n, config.G, config.epsilon);
    const double loop_start = now_seconds();
    force_seconds += loop_start - total_start;

    for (int step = 0; step < config.steps; ++step) {
        t0 = now_seconds();
        particles_kick(&particles, 0, n, half_dt);
        particles_drift(&particles, 0, n, config.dt);
        integration_seconds += now_seconds() - t0;

        t0 = now_seconds();
        compute_accelerations_openmp(&particles, 0, n, config.G,
                                     config.epsilon);
        force_seconds += now_seconds() - t0;

        t0 = now_seconds();
        particles_kick(&particles, 0, n, half_dt);
        integration_seconds += now_seconds() - t0;
    }
    const double total_end = now_seconds();

    const RunReport run = {
        .backend = "openmp",
        .mpi_ranks = 1,
        .omp_threads = omp_get_max_threads(),
        .nodes = 1,
        .gpu_model = NULL,
        .elapsed_seconds = total_end - total_start,
        .loop_seconds = total_end - loop_start,
        .force_seconds = force_seconds,
        .communication_seconds = 0.0,
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
