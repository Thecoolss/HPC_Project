#!/usr/bin/env python3
"""Make report figures 1-3 from the scaling and serial CSV files.

Figure 1: OpenMP speedup and efficiency against threads (E2).
Figure 2: pure MPI speedup against ranks, with the ideal line (E3).
Figure 3: time per step for each ranks x threads split, 1 and 2 nodes (E4, E5).

Usage, from the repository root on the cluster:
    module load matplotlib/3.7.2-gfbf-2023a
    python3 scripts/plot_results.py
The PNG files are written to results/figures/.
"""

import csv
import os
import statistics

import matplotlib
matplotlib.use("Agg")  # no screen on the cluster: draw straight to files
import matplotlib.pyplot as plt

SCALING_CSV = "results/scaling_runs.csv"
SERIAL_CSV = "results/serial_runs.csv"
FIGURE_DIR = "results/figures"


def read_timed_runs(path):
    """Read a CSV made by logs_to_csv.py and keep only the timed runs."""
    with open(path, newline="", encoding="utf-8") as csv_file:
        return [row for row in csv.DictReader(csv_file)
                if row["run_kind"] == "timed"]


def median_time(runs, **match):
    """Median seconds_per_step of the runs whose columns have these values.

    Example: median_time(runs, backend="openmp", n=20000, omp_threads=4)
    """
    times = [float(run["seconds_per_step"]) for run in runs
             if all(run[key] == str(value) for key, value in match.items())]
    if not times:
        raise ValueError(f"no timed runs found for {match}")
    return statistics.median(times)


def save(figure, name):
    path = os.path.join(FIGURE_DIR, name)
    figure.tight_layout()
    figure.savefig(path, dpi=150)
    plt.close(figure)
    print(f"wrote {path}")


def figure_openmp(runs):
    """Figure 1: OpenMP speedup and efficiency, N = 20,000, one node (E2)."""
    threads = [1, 2, 4, 8, 16, 32]
    times = [median_time(runs, backend="openmp", n=20000, mpi_ranks=1,
                         omp_threads=t) for t in threads]

    # Speedup against the 1-thread run of the same job and node.
    speedup = [times[0] / t for t in times]
    efficiency = [100 * s / p for s, p in zip(speedup, threads)]

    figure, (left, right) = plt.subplots(1, 2, figsize=(10, 4))

    left.plot(threads, threads, "k--", label="ideal")
    left.plot(threads, speedup, "o-", label="OpenMP")
    left.set_yscale("log", base=2)
    left.set_yticks(threads, [str(t) for t in threads])
    left.set_xlabel("OpenMP threads")
    left.set_ylabel("speedup over 1 thread")
    left.legend()

    right.plot(threads, efficiency, "o-")
    right.set_xlabel("OpenMP threads")
    right.set_ylabel("parallel efficiency (%)")
    right.set_ylim(0, 110)

    for axis in (left, right):
        axis.set_xscale("log", base=2)
        axis.set_xticks(threads, [str(t) for t in threads])
        axis.grid(True, alpha=0.3)

    figure.suptitle("Figure 1: OpenMP scaling, N = 20,000, one node (E2)")
    save(figure, "fig1_openmp_scaling.png")


def figure_mpi(runs, serial_runs):
    """Figure 2: pure MPI speedup over serial, N = 100,000, one node (E3)."""
    serial_time = median_time(serial_runs, backend="serial", n=100000)

    ranks = [2, 4, 8, 16, 32]
    times = [median_time(runs, backend="hybrid", n=100000, nodes=1,
                         mpi_ranks=r, omp_threads=1) for r in ranks]

    # Rank 1 is the serial run itself: speedup 1 by definition.
    ranks = [1] + ranks
    speedup = [1.0] + [serial_time / t for t in times]

    figure, axis = plt.subplots(figsize=(6, 4.5))
    axis.plot(ranks, ranks, "k--", label="ideal")
    axis.plot(ranks, speedup, "o-", label="pure MPI (1 thread per rank)")
    axis.set_xscale("log", base=2)
    axis.set_yscale("log", base=2)
    axis.set_xticks(ranks, [str(r) for r in ranks])
    axis.set_yticks(ranks, [str(r) for r in ranks])
    axis.set_xlabel("MPI ranks")
    axis.set_ylabel("speedup over serial")
    axis.grid(True, alpha=0.3)
    axis.legend()
    axis.set_title("Figure 2: MPI strong scaling, N = 100,000 (E3)")
    save(figure, "fig2_mpi_scaling.png")


def figure_splits(runs):
    """Figure 3: seconds per step for each ranks x threads split (E4, E5)."""
    splits = [(1, 32), (2, 16), (4, 8), (8, 4), (16, 2), (32, 1)]
    labels = [f"{r}x{t}" for r, t in splits]
    positions = list(range(len(splits)))
    width = 0.38

    figure, axis = plt.subplots(figsize=(8, 4.5))
    for nodes, shift in [(1, -width / 2), (2, width / 2)]:
        xs, ys = [], []
        for position, (r, t) in zip(positions, splits):
            try:
                ys.append(median_time(runs, backend="hybrid", n=100000,
                                      nodes=nodes, mpi_ranks=r, omp_threads=t))
            except ValueError:
                continue  # e.g. 1x32 cannot run on 2 nodes
            xs.append(position + shift)
        name = "1 node" if nodes == 1 else "2 nodes (16 + 16 cores)"
        bars = axis.bar(xs, ys, width, label=name)
        axis.bar_label(bars, fmt="%.3f", fontsize=8)

    axis.set_xticks(positions, labels)
    axis.set_xlabel("MPI ranks x OpenMP threads (32 cores in total)")
    axis.set_ylabel("median seconds per step")
    axis.set_ylim(0, 1.35)
    axis.legend()
    axis.set_title("Figure 3: ranks x threads splits, N = 100,000 (E4, E5)")
    save(figure, "fig3_splits_nodes.png")


def main():
    os.makedirs(FIGURE_DIR, exist_ok=True)
    runs = read_timed_runs(SCALING_CSV)
    serial_runs = read_timed_runs(SERIAL_CSV)
    figure_openmp(runs)
    figure_mpi(runs, serial_runs)
    figure_splits(runs)


if __name__ == "__main__":
    main()
