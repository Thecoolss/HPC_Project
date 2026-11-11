# Gravitational N-body simulation

Direct all-pairs (`O(N^2)`) softened Newtonian N-body simulation in C11 with
kick-drift-kick leapfrog integration and Structure-of-Arrays storage. The
serial reference is implemented; OpenMP, hybrid MPI+OpenMP and OpenACC
backends follow the contract in [docs/INTERFACE.md](docs/INTERFACE.md).

## Build and test

Requirements: a C11 compiler, GNU Make, the C math library and Python 3 (for
`scripts/compare_states.py`).

```sh
. env/modules.sh   # on the cluster: OpenMPI/4.1.5-GCC-12.3.0
make               # serial, OpenMP and hybrid MPI+OpenMP
make test          # unit and CLI tests (no MPI or GPU needed)
```

The OpenACC backend uses its own environment: `. env/modules_openacc.sh &&
make openacc`. `make test-hybrid` runs only the `partition_range` tests;
`tests/test_hybrid.sh` checks the MPI backend against serial inside a Slurm
allocation.

Flags: `-std=c11 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes
-Werror`. `make WERROR=` disables `-Werror`; `make OPTFLAGS=-O3` changes the
optimization level. The build embeds the Git commit, compiler and flags into
the binary.

## Run

```sh
./build/nbody_serial --n 1000 --steps 100 --dt 0.001 --eps 0.01 --seed 42 --check
```

| Option | Default | Meaning |
|---|---:|---|
| `--n` | 1000 | Number of particles; at least 1 |
| `--steps` | 100 | Number of KDK steps; at least 1 |
| `--dt` | 0.001 | Positive timestep |
| `--eps` | 0.01 | Positive Plummer softening |
| `--seed` | 42 | Unsigned 64-bit initialization seed |
| `--check` | off | Add initial/final energy and momentum |
| `--output FILE` | off | Write the final positions and velocities |

The program prints one line of `key=value` fields (configuration, layout,
host, Slurm job ID, Git commit, build flags, timings). The timer covers the
initial force evaluation and all steps. Initialization, diagnostics and output
are not timed. [docs/INTERFACE.md](docs/INTERFACE.md) defines every field.

Compare two final states (for example serial against a parallel backend):

```sh
./build/nbody_serial --n 1000 --output ref.txt
python3 scripts/compare_states.py ref.txt other.txt
```

## Slurm

```sh
. env/modules.sh
make test
sbatch slurm/serial.sbatch                                   # N=1000
sbatch --time=00:30:00 --export=ALL,N=10000 slurm/serial.sbatch
```

The job writes `results/logs/serial_<jobid>.out` containing job metadata
(host, modules, CPU, Git revision), one `--check` run and three timed runs.
Run significant workloads through Slurm, not on the login node.

## Layout

- `include/`: shared headers (`nbody.h`, `config.h`, `report.h`).
- `src/`: shared modules, then `force_<backend>.c` and `main_<backend>.c` per
  backend.
- `tests/`: unit and CLI tests.
- `env/`: cluster module environment.
- `slurm/`: job scripts.
- `scripts/`: job metadata and state comparison.
- `results/`: logs, CSV and plots.
- `docs/`: interface contract and reports.
