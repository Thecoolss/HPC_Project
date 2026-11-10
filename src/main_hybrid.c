/*
 * Hybrid MPI + OpenMP backend (docs/INTERFACE.md §11, "Hybrid MPI+OpenMP").
 *
 * - Every rank initializes the full system with the same seed (no broadcast).
 * - Rank r of P owns [begin, end) = partition_range(n, P, r). Its velocities
 *   and accelerations are authoritative only in that range.
 * - Each rank kicks, drifts and computes forces for its own range only, using
 *   the OpenMP kernel (threads inside the rank).
 * - After every drift, positions are synchronized with MPI_Allgatherv.
 * - Timings use MPI_Wtime and report the maximum over ranks.
 * - Only rank 0 prints the result line or writes --output.
 *
 * Communication variant measured: baseline (three Allgatherv calls, x/y/z).
 */
#include "config.h"
#include "nbody.h"
#include "report.h"

#include <mpi.h>
#include <omp.h>

#include <stdio.h>
#include <stdlib.h>

/* Order of the timing fields reduced across ranks. */
enum {
    T_ELAPSED,
    T_LOOP,
    T_FORCE,
    T_COMM,
    T_INTEGRATION,
    T_COUNT
};

/*
 * Fills counts[r] and displs[r] for every rank r from partition_range, as
 * MPI_Allgatherv/MPI_Gatherv expect: counts[r] = end - begin, displs[r] =
 * begin.
 */
static void build_counts(int n, int ranks, int *counts, int *displs)
{
    for (int r = 0; r < ranks; ++r) {
        int begin = 0;
        int end = 0;
        partition_range(n, ranks, r, &begin, &end);
        counts[r] = end - begin;
        displs[r] = begin;
    }
}

/*
 * Makes every rank's x, y, z arrays identical again after each rank drifted
 * its own range. Baseline variant: three MPI_Allgatherv calls (x, y, z) with
 * MPI_IN_PLACE, since each rank's own block already sits at displs[rank].
 * Masses never change and are not exchanged.
 *
 * Optimization candidate (Days 9-10, only if profiling shows comm matters):
 * pack x, y, z into one buffer and do a single Allgatherv. Record which
 * variant was measured.
 */
static void exchange_positions(Particles *p, const int *counts,
                               const int *displs, MPI_Comm comm)
{
    MPI_Allgatherv(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, p->x, counts, displs,
                   MPI_DOUBLE, comm);
    MPI_Allgatherv(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, p->y, counts, displs,
                   MPI_DOUBLE, comm);
    MPI_Allgatherv(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, p->z, counts, displs,
                   MPI_DOUBLE, comm);
}

/*
 * Collects the owned velocities of every rank into rank 0's vx, vy, vz so
 * rank 0 can compute energy/momentum (--check) and write --output. Positions
 * are already replicated after the last exchange. Use MPI_Gatherv with
 * MPI_IN_PLACE on rank 0.
 */
static void gather_velocities(Particles *p, const int *counts,
                              const int *displs, int rank, MPI_Comm comm)
{
    double *arrays[3] = {p->vx, p->vy, p->vz};
    for (int k = 0; k < 3; ++k) {
        if (rank == 0) {
            MPI_Gatherv(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, arrays[k], counts,
                        displs, MPI_DOUBLE, 0, comm);
        } else {
            MPI_Gatherv(arrays[k] + displs[rank], counts[rank], MPI_DOUBLE,
                        NULL, NULL, NULL, MPI_DOUBLE, 0, comm);
        }
    }
}

/*
 * Replaces values[0 .. count-1] on rank 0 with the maximum over all ranks
 * (MPI_Reduce with MPI_MAX; MPI_IN_PLACE on rank 0).
 */
static void reduce_max_timings(double *values, int count, int rank,
                               MPI_Comm comm)
{
    if (rank == 0) {
        MPI_Reduce(MPI_IN_PLACE, values, count, MPI_DOUBLE, MPI_MAX, 0, comm);
    } else {
        MPI_Reduce(values, NULL, count, MPI_DOUBLE, MPI_MAX, 0, comm);
    }
}

/* Number of distinct nodes in the job (one shared-memory communicator per
 * node). */
static int count_nodes(MPI_Comm comm)
{
    MPI_Comm node_comm;
    MPI_Comm_split_type(comm, MPI_COMM_TYPE_SHARED, 0, MPI_INFO_NULL,
                        &node_comm);
    int node_rank = 0;
    MPI_Comm_rank(node_comm, &node_rank);
    MPI_Comm_free(&node_comm);

    int is_leader = node_rank == 0 ? 1 : 0;
    int nodes = 0;
    MPI_Allreduce(&is_leader, &nodes, 1, MPI_INT, MPI_SUM, comm);
    return nodes;
}

int main(int argc, char **argv)
{
    /* Only the main thread calls MPI; OpenMP threads only run the kernel. */
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    const MPI_Comm comm = MPI_COMM_WORLD;

    int rank = 0;
    int ranks = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &ranks);

    if (provided < MPI_THREAD_FUNNELED) {
        if (rank == 0) {
            fprintf(stderr, "MPI does not provide MPI_THREAD_FUNNELED\n");
        }
        MPI_Abort(comm, EXIT_FAILURE);
    }

    /* Every rank parses the same argv, so they all agree on the config. */
    SimulationConfig config;
    RunOptions options;
    const ConfigStatus status = config_parse(argc, argv, &config, &options);
    if (status != CONFIG_OK) {
        if (status == CONFIG_INVALID && rank == 0) {
            config_print_usage(argv[0]);
            fprintf(stderr, "Invalid command-line arguments\n");
        }
        MPI_Finalize();
        return status == CONFIG_HELP ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    /* Same seed on every rank -> bit-identical full arrays, no broadcast. */
    Particles particles = {0};
    if (!particles_alloc(&particles, config.n) ||
        !particles_init_cube(&particles, config.seed)) {
        fprintf(stderr, "rank %d: failed to set up particle arrays\n", rank);
        MPI_Abort(comm, EXIT_FAILURE);
    }

    const int n = particles.n;
    int begin = 0;
    int end = n;
    partition_range(n, ranks, rank, &begin, &end);

    int *counts = malloc((size_t)ranks * sizeof *counts);
    int *displs = malloc((size_t)ranks * sizeof *displs);
    if (counts == NULL || displs == NULL) {
        fprintf(stderr, "rank %d: failed to allocate counts\n", rank);
        MPI_Abort(comm, EXIT_FAILURE);
    }
    build_counts(n, ranks, counts, displs);

    /* The initial state is identical everywhere, so rank 0 can compute the
     * initial diagnostics from its own full copy. */
    CheckReport check = {0};
    if (options.check && rank == 0) {
        check.initial_energy =
            compute_total_energy(&particles, config.G, config.epsilon);
        compute_total_momentum(&particles, check.initial_momentum);
    }

    const double half_dt = 0.5 * config.dt;
    double times[T_COUNT] = {0.0};
    double t0;

    /* Start all ranks together so the timers measure the same interval. */
    MPI_Barrier(comm);

    /* Primary timer: initial force evaluation plus all KDK steps. */
    const double total_start = MPI_Wtime();
    compute_accelerations_openmp(&particles, begin, end, config.G,
                                 config.epsilon);
    const double loop_start = MPI_Wtime();
    times[T_FORCE] += loop_start - total_start;

    for (int step = 0; step < config.steps; ++step) {
        t0 = MPI_Wtime();
        particles_kick(&particles, begin, end, half_dt);
        particles_drift(&particles, begin, end, config.dt);
        times[T_INTEGRATION] += MPI_Wtime() - t0;

        t0 = MPI_Wtime();
        exchange_positions(&particles, counts, displs, comm);
        times[T_COMM] += MPI_Wtime() - t0;

        t0 = MPI_Wtime();
        compute_accelerations_openmp(&particles, begin, end, config.G,
                                     config.epsilon);
        times[T_FORCE] += MPI_Wtime() - t0;

        t0 = MPI_Wtime();
        particles_kick(&particles, begin, end, half_dt);
        times[T_INTEGRATION] += MPI_Wtime() - t0;
    }
    const double total_end = MPI_Wtime();
    times[T_ELAPSED] = total_end - total_start;
    times[T_LOOP] = total_end - loop_start;

    /* Collective calls: every rank must take part, outside the timers. */
    reduce_max_timings(times, T_COUNT, rank, comm);
    const int nodes = count_nodes(comm);
    if (options.check || options.output_path != NULL) {
        gather_velocities(&particles, counts, displs, rank, comm);
    }

    int exit_code = EXIT_SUCCESS;
    if (rank == 0) {
        const RunReport run = {
            .backend = "hybrid",
            .mpi_ranks = ranks,
            .omp_threads = omp_get_max_threads(),
            .nodes = nodes,
            .gpu_model = NULL,
            .elapsed_seconds = times[T_ELAPSED],
            .loop_seconds = times[T_LOOP],
            .force_seconds = times[T_FORCE],
            .communication_seconds = times[T_COMM],
            .integration_seconds = times[T_INTEGRATION]
        };

        if (options.check) {
            check.final_energy =
                compute_total_energy(&particles, config.G, config.epsilon);
            compute_total_momentum(&particles, check.final_momentum);
        }
        report_print(stdout, &config, &run, options.check ? &check : NULL);

        if (options.output_path != NULL &&
            !report_write_state(options.output_path, &config, run.backend,
                                &particles)) {
            fprintf(stderr, "Failed to write final state to %s\n",
                    options.output_path);
            exit_code = EXIT_FAILURE;
        }
    }

    free(counts);
    free(displs);
    particles_free(&particles);
    MPI_Finalize();
    return exit_code;
}
