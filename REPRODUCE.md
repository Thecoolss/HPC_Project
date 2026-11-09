# Reproducing the experiments

## Environment

| Item | Value |
|---|---|
| Cluster | SciTech (record partition/node from the job log) |
| Modules | `env/modules.sh` (currently `GCC/12.3.0`, to be verified) |
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

| Experiment | Commit | Job ID | Modules | Log |
|---|---|---|---|---|
| Serial baseline N=1000 | _pending_ | _pending_ | _pending_ | _pending_ |

Local development checks used Debian GCC 12.2.0 (WSL). They are not reported
results.
