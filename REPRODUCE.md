# Reproducing the experiments

## Environment

| Item | Value |
|---|---|
| Cluster | SciTech, partition `cpu`, node `haskell`: 2 × AMD EPYC 9755 (128 cores each, SMT 2, 512 logical CPUs, 2 NUMA nodes) |
| Modules | `env/modules.sh`: `GCC/12.3.0` (loads `GCCcore/12.3.0`) |
| Compiler flags | `-std=c11 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Werror` |
| Seed | 42 (all randomness comes from `--seed`) |

Each binary prints its Git commit (`git describe --always --dirty`), compiler,
version and flags. Each job log also records host, CPU, modules and Slurm
allocation through `scripts/job_metadata.sh`.

## Build and test

```sh
git clone <repository-url> && cd <repository>
git checkout <commit-or-tag>
. env/modules.sh
make clean
make test
```

## Serial baseline

```sh
sbatch slurm/serial.sbatch
```

Command per run: `./build/nbody_serial --n 1000 --steps 100 --dt 0.001 --eps
0.01 --seed 42`, one `--check` run and three timed repetitions on one core
(`--ntasks=1 --cpus-per-task=1 --mem=1G`). The log is
`results/logs/serial_<jobid>.out`.

## Recorded runs

| Experiment | Commit | Job ID | Node | Modules | Log |
|---|---|---|---|---|---|
| Serial baseline N=1000, 100 steps | `0f39c80938b5` | 28010 | `haskell` | `GCC/12.3.0` | `results/logs/serial_28010.out` |

Serial baseline result (job 28010, 3 timed repetitions):

| Metric | Median | Range |
|---|---:|---|
| `seconds_per_step` | 3.0747e-3 s | 3.0696e-3 to 3.0751e-3 s (0.18%) |
| `interactions_per_second` | 3.249e8 | 3.249e8 to 3.255e8 |
| `elapsed_seconds` | 0.3105 s | 0.3100 to 0.3106 s |

The force kernel takes 99.9% of the elapsed time (`force_seconds` 0.3103 s of
0.3105 s). The `--check` run gives a relative energy change of -1.18e-8 over
100 steps, and momentum stays below 1.3e-18. Initial and final energies match
the local GCC 12.2.0 build bit for bit, so the result does not depend on that
machine or compiler version.

Local development checks used Debian GCC 12.2.0 (WSL). They are not reported
results.
