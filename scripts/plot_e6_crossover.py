#!/usr/bin/env python3
"""Build the E6 GPU-vs-CPU crossover figure from raw Slurm .out logs.

It reads one or more log files, picks out the timed result lines (the same
contract scripts/logs_to_csv.py uses: a line starting with "backend=", parsed
with shlex; a line with an "initial_energy" key is a --check run and is
skipped), takes the median seconds_per_step per (backend, n), and writes:

  <out>.csv   backend,n,steps,reps,median_seconds_per_step,median_interactions_per_second
  <out>.png   log-log seconds_per_step vs N, one line per backend (if matplotlib)

Usage:
  python3 scripts/plot_e6_crossover.py results/logs/e6_openacc_*.out \
      results/logs/e6_openmp_*.out results/logs/serial_*.out --out results/e6_crossover

Stdlib only for parsing/CSV; matplotlib is optional and only used for the PNG.
"""
import argparse
import csv
import glob
import shlex
import statistics
import sys


def parse_line(line):
    fields = {}
    for tok in shlex.split(line):
        if "=" in tok:
            k, v = tok.split("=", 1)
            fields[k] = v
    return fields


def read_timed_runs(paths):
    runs = []
    for pattern in paths:
        for path in sorted(glob.glob(pattern)) or [pattern]:
            try:
                with open(path, encoding="utf-8", errors="replace") as fh:
                    for line in fh:
                        if not line.startswith("backend="):
                            continue
                        f = parse_line(line.strip())
                        if "initial_energy" in f:   # --check run, not a timing
                            continue
                        if "seconds_per_step" not in f or "n" not in f:
                            continue
                        runs.append(f)
            except OSError as exc:
                print(f"warning: cannot read {path}: {exc}", file=sys.stderr)
    return runs


def summarize(runs):
    groups = {}
    for r in runs:
        key = (r.get("backend", ""), int(r["n"]), r.get("steps", ""))
        groups.setdefault(key, []).append(r)
    rows = []
    for (backend, n, steps), rs in sorted(groups.items(), key=lambda k: (k[0][0], k[0][1])):
        sps = [float(r["seconds_per_step"]) for r in rs]
        ips = [float(r["interactions_per_second"]) for r in rs
               if "interactions_per_second" in r]
        rows.append({
            "backend": backend,
            "n": n,
            "steps": steps,
            "reps": len(rs),
            "median_seconds_per_step": statistics.median(sps),
            "median_interactions_per_second":
                statistics.median(ips) if ips else "",
        })
    return rows


def write_csv(rows, out):
    path = out + ".csv"
    with open(path, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=[
            "backend", "n", "steps", "reps",
            "median_seconds_per_step", "median_interactions_per_second"])
        w.writeheader()
        w.writerows(rows)
    print(f"wrote {path} ({len(rows)} configurations)")


def write_png(rows, out):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not available; skipping PNG (CSV still written)",
              file=sys.stderr)
        return
    by_backend = {}
    for r in rows:
        by_backend.setdefault(r["backend"], []).append(r)
    fig, ax = plt.subplots(figsize=(7, 5))
    for backend, rs in sorted(by_backend.items()):
        rs.sort(key=lambda r: r["n"])
        ax.plot([r["n"] for r in rs],
                [r["median_seconds_per_step"] for r in rs],
                marker="o", label=backend)
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("N (particles)")
    ax.set_ylabel("median seconds per step")
    ax.set_title("E6: time per step vs N (GPU vs CPU crossover)")
    ax.grid(True, which="both", ls=":", alpha=0.5)
    ax.legend()
    fig.tight_layout()
    path = out + ".png"
    fig.savefig(path, dpi=150)
    print(f"wrote {path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+", help="Slurm .out log files or globs")
    ap.add_argument("--out", default="e6_crossover",
                    help="output path prefix (default: e6_crossover)")
    args = ap.parse_args()
    runs = read_timed_runs(args.logs)
    if not runs:
        print("no timed result lines found", file=sys.stderr)
        sys.exit(1)
    rows = summarize(runs)
    write_csv(rows, args.out)
    write_png(rows, args.out)


if __name__ == "__main__":
    main()
