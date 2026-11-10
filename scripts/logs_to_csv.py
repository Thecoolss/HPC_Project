#!/usr/bin/env python3
"""Collect nbody result lines from Slurm logs into a CSV and a summary table.

Usage:
    python3 scripts/logs_to_csv.py results/logs/serial_*.out \
        --csv results/serial_runs.csv --summary results/serial_summary.md

Every line starting with "backend=" is one run. Runs with energy diagnostics
are labelled run_kind=check; the others are run_kind=timed. The summary uses
the median of the timed runs for each configuration. Standard library only.
"""

import argparse
import csv
import os
import shlex
import statistics
import sys


def parse_result_line(line):
    """Turn 'key=value key="quoted value" ...' into a dictionary.

    shlex.split is used instead of line.split() because some values contain
    spaces inside quotes, for example build_flags="-std=c11 -O2 ...".
    """
    fields = {}
    for token in shlex.split(line):
        if "=" in token:
            key, value = token.split("=", 1)
            fields[key] = value
    return fields


def read_runs(log_paths):
    """Read every result line from every log file."""
    runs = []
    for path in log_paths:
        with open(path, encoding="utf-8", errors="replace") as log_file:
            for line in log_file:
                if not line.startswith("backend="):
                    continue  

                run = parse_result_line(line)

                
                if "initial_energy" in run:
                    run["run_kind"] = "check"
                else:
                    run["run_kind"] = "timed"

                run["log"] = os.path.basename(path)
                runs.append(run)
    return runs


def write_csv(runs, csv_path):
    """Write all runs to a CSV file, one row per run."""
    
    columns = []
    for run in runs:
        for key in run:
            if key not in columns:
                columns.append(key)

    with open(csv_path, "w", newline="", encoding="utf-8") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=columns)
        writer.writeheader()
        writer.writerows(runs)


def group_timed_runs(runs):
    """Group the timed runs that share the same configuration.

    Returns a dictionary {configuration: [run, run, ...]}.
    """
    groups = {}
    for run in runs:
        if run["run_kind"] != "timed":
            continue

        configuration = (
            run.get("backend", ""),
            run.get("n", ""),
            run.get("steps", ""),
            run.get("mpi_ranks", ""),
            run.get("omp_threads", ""),
            run.get("nodes", ""),
            run.get("host", ""),
            run.get("git_commit", ""),
        )
        if configuration not in groups:
            groups[configuration] = []
        groups[configuration].append(run)
    return groups


def sort_order(configuration):
    """Sort by backend, then numerically by N, MPI ranks and OpenMP threads."""
    backend, n, steps, ranks, threads, nodes, host, commit = configuration
    return (backend, int(n), int(ranks), int(threads))


def make_summary_table(runs):
    """Build a Markdown table with one row per configuration."""
    lines = [
        "| backend | N | steps | ranks | threads | host | commit | reps "
        "| median s/step | spread | median interactions/s | job |",
        "|---|---:|---:|---:|---:|---|---|---:|---:|---:|---:|---|",
    ]

    groups = group_timed_runs(runs)
    for configuration in sorted(groups, key=sort_order):
        group = groups[configuration]
        backend, n, steps, ranks, threads, nodes, host, commit = configuration

        seconds_per_step = [float(run["seconds_per_step"]) for run in group]
        interactions = [float(run["interactions_per_second"]) for run in group]

        median_time = statistics.median(seconds_per_step)
        median_interactions = statistics.median(interactions)
        spread = (max(seconds_per_step) - min(seconds_per_step)) / median_time

        job_ids = sorted(set(run.get("slurm_job_id", "?") for run in group))
        jobs = ",".join(job_ids)

        lines.append(
            f"| {backend} | {n} | {steps} | {ranks} | {threads} | {host} "
            f"| {commit} | {len(group)} | {median_time:.6g} | {spread:.1%} "
            f"| {median_interactions:.4g} | {jobs} |")

    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("logs", nargs="+", help="Slurm log files to read")
    parser.add_argument("--csv", help="write every run to this CSV file")
    parser.add_argument("--summary", help="write the Markdown table here")
    args = parser.parse_args()

    runs = read_runs(args.logs)
    if not runs:
        print("error: no 'backend=' result lines found", file=sys.stderr)
        return 1

    if args.csv:
        write_csv(runs, args.csv)

    table = make_summary_table(runs)
    if args.summary:
        with open(args.summary, "w", encoding="utf-8") as summary_file:
            summary_file.write(table)

    print(table, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
