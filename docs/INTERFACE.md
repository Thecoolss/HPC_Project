# N-body C interface and backend contract

This is the repository's authoritative contract for the serial reference and
the OpenMP, hybrid MPI+OpenMP and OpenACC backends. Change it only through a
reviewed pull request that also updates `include/nbody.h`.

## 1. Repository layout and ownership

All backends live in a flat `src/` directory and follow one naming pattern:
`force_<backend>.c` holds the force kernel, `main_<backend>.c` holds the
backend-specific simulation loop, and the executable is `nbody_<backend>`.
Backend names are `serial`, `openmp`, `hybrid` and `openacc`.

| Path | Contents | Owner |
|---|---|---|
| `include/nbody.h` | Particle/config types, shared physics API, all force and partition signatures | Interface/serial |
| `include/config.h`, `src/config.c` | Shared CLI parser (`--n --steps --dt --eps --seed --check --output`) | Interface/serial |
| `include/report.h`, `src/report.c` | Shared `key=value` result line and `--output` state file | Interface/serial |
| `src/particles.c` | Allocation, free, seeded initialization | Interface/serial |
| `src/integrator.c` | `particles_kick`, `particles_drift` | Interface/serial |
| `src/diagnostics.c` | Energy and momentum | Interface/serial |
| `src/force_serial.c`, `src/main_serial.c` | Serial reference | Interface/serial |
| `src/force_openmp.c`, `src/main_openmp.c` | OpenMP kernel and loop | OpenMP |
| `src/decomposition.c`, `src/main_hybrid.c` | `partition_range`, MPI loop and communication | MPI |
| `src/force_openacc.c`, `src/main_openacc.c` | OpenACC kernel, device data management | OpenACC |
| `tests/test_nbody.c`, `tests/test_cli.sh` | Shared and serial tests | Interface/serial |
| `tests/test_<backend>.c` | Backend tests | Backend owner |
| `Makefile`, `scripts/compare_states.py`, `REPRODUCE.md` | Build targets, cross-backend comparison, reproduction | Integration |
| `env/modules.sh` | Cluster modules used by every build and job | Integration |
| `slurm/<backend>.sbatch`, `scripts/job_metadata.sh`, `scripts/` experiment tools | Job templates, metadata, sweeps, CSV, plots | Benchmark |
| `results/` | CSV, plots, kept job logs (`results/logs/`) | Benchmark |

An optimized kernel variant (for example a SIMD version) is added as a separate
function or build target so the before-version stays reproducible.

## 2. Data types and memory ownership

`Particles` stores ten length-`n` `double` arrays in Structure-of-Arrays form.
After a successful `particles_alloc` the value owns every array. Initialize
with `Particles p = {0};`, allocate once, and call `particles_free`.
`particles_free` accepts a null pointer or a partially allocated value and
resets every field.

`SimulationConfig` holds `n`, `steps`, `dt`, `epsilon`, `G` and `seed`. `G` is
fixed at `1.0` and is not a CLI option.

## 3. Public functions

```c
int  particles_alloc(Particles *p, int n);
void particles_free(Particles *p);
int  particles_init_cube(Particles *p, uint64_t seed);

void particles_kick(Particles *p, int begin, int end, double half_dt);
void particles_drift(Particles *p, int begin, int end, double dt);

double compute_total_energy(const Particles *p, double G, double epsilon);
void   compute_total_momentum(const Particles *p, double out[3]);

void compute_accelerations_serial (Particles *p, int begin, int end, double G, double epsilon);
void compute_accelerations_openmp (Particles *p, int begin, int end, double G, double epsilon);
void compute_accelerations_openacc(Particles *p, int begin, int end, double G, double epsilon);

void partition_range(int n, int parts, int part, int *begin, int *end);
```

- `particles_alloc` and `particles_init_cube` return nonzero on success. A
  failed allocation leaves `p` zeroed and releases every partial allocation.
- Ranges are half-open with `0 <= begin <= end <= n`; empty and single-element
  ranges are valid. Callers guarantee valid ranges.
- Every `compute_accelerations_*` reads all positions and masses, excludes
  `j == i`, **overwrites** `ax/ay/az` for targets in `[begin, end)` only, and
  never changes positions, velocities or masses. Distinct targets must not
  race.
- `particles_kick` updates only velocities in its range by
  `half_dt * acceleration`; `particles_drift` updates only positions in its
  range by `dt * velocity`.
- `compute_accelerations_openacc` must be correct whether or not the arrays are
  already on the device (OpenACC `copyin`/`copyout` clauses act as
  present-or-copy inside an enclosing data region).
- `partition_range` assigns part `part` of `parts`:
  `base = n / parts`, `extra = n % parts`,
  `begin = part * base + min(part, extra)`,
  `end = begin + base + (part < extra ? 1 : 0)`.
  The first `extra` parts get one extra particle, and the union of all parts is
  exactly `[0, n)`.

## 4. Initial conditions

Every backend must produce bit-identical initial arrays for the same `n` and
seed. The generator is SplitMix64, a small, well-known 64-bit generator chosen
because it is easy to reimplement exactly. It is defined in `src/particles.c`:

```c
state = seed;
next():                                   // 64-bit unsigned, wraps mod 2^64
    state += 0x9e3779b97f4a7c15;
    z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
uniform():                                // double in [0, 1)
    return (next() >> 11) * 2^-53;        // top 53 bits = full double mantissa

for i in 0 .. n-1:                        // four draws per particle, this order
    x[i] = 2*uniform() - 1;  y[i] = 2*uniform() - 1;  z[i] = 2*uniform() - 1;
    mass[i] = 0.5 + uniform();            // raw weight in [0.5, 1.5)
mass[i] /= sum(mass)                      // total mass = 1
v = 0, a = 0
```

Positions therefore lie in `[-1, 1)^3`. Particle `i` always gets the same
values, regardless of how many ranks or threads run the simulation.

## 5. Physics and integration order

```text
a_i = G * sum(j != i) m_j * (r_j - r_i) / (|r_j - r_i|^2 + epsilon^2)^(3/2)
```

The diagnostics use the matching softened potential
`-G m_i m_j / sqrt(|r_j - r_i|^2 + epsilon^2)` summed over unordered pairs plus
kinetic energy, and total momentum `sum m_i v_i`.

```text
force                         # initial evaluation, inside the timer
repeat steps times:
    kick(dt/2); drift(dt); [exchange positions]; force; kick(dt/2)
```

## 6. Defaults and their validation

Defaults: `G = 1`, `epsilon = 0.01`, `dt = 0.001`, `n = 1000`, `steps = 100`,
`seed = 42`.

The initial state is a cold (zero-velocity) uniform cube, which collapses
under its own gravity in about one free-fall time, `t_ff ≈ 1.5`. The defaults
were checked with the serial code at `n = 1000` up to `t = 2`, past the
collapse:

| epsilon | dt = 0.002 | dt = 0.001 | dt = 0.0005 | dt = 0.00025 |
|---|---:|---:|---:|---:|
| 0.01 | 6.8e-6 | 6.3e-6 | 1.8e-6 | 4.5e-7 |
| 0.02 | 2.9e-5 | 6.9e-6 | 1.7e-6 | 4.3e-7 |
| 0.05 | 3.4e-6 | 8.5e-7 | 2.1e-7 | 5.3e-8 |

Values are the relative energy change |ΔE/E₀| at t = 2. With the defaults, the
largest error seen during the run was 5.4e-6 at t = 1.5, the moment of
collapse. Halving `dt` below 0.001 reduces the error about 4x, as expected for
a second-order integrator. Larger `n` is easier to integrate because each
particle's mass `1/n` is smaller. The default run (100 steps, t = 0.1) covers
only the early infall: use it for timing, and use `--steps 2000` for accuracy
checks through the collapse. `epsilon` and `dt` do not change the work per
step.

## 7. Timing

- `elapsed_seconds`: initial force evaluation plus all steps.
- `loop_seconds`: all steps without the initial force evaluation.
- `seconds_per_step = loop_seconds / steps`.
- `interactions_per_second = n * (n - 1) * steps / loop_seconds`, computed in
  `double`.
- `force_seconds` (including the initial evaluation), `communication_seconds`
  and `integration_seconds` split the work by phase.

Allocation, initialization, diagnostics, `--output` and printing are outside
every timer. Use a monotonic clock (`clock_gettime(CLOCK_MONOTONIC)`, or
`MPI_Wtime` in the hybrid code). Distributed backends report the maximum over
ranks for every timing field.

## 8. Command line

```text
nbody_<backend> [--n N] [--steps S] [--dt DT] [--eps EPS] [--seed SEED]
                [--check] [--output FILE]
```

All backends parse arguments with `config_parse` and must reject invalid
values with a nonzero exit status. Thread, rank and GPU counts come from the
environment (`OMP_NUM_THREADS`, `srun`, the Slurm allocation), not from options.

## 9. Result line

Each backend prints one line through `report_print`. Fields appear in this
order:

```text
backend n steps G dt epsilon seed mpi_ranks omp_threads nodes gpu_model host
slurm_job_id git_commit compiler compiler_version build_flags
elapsed_seconds loop_seconds seconds_per_step interactions_per_second
force_seconds communication_seconds integration_seconds
```

With `--check`, these fields follow:

```text
initial_energy final_energy energy_change relative_energy_change
initial_momentum_x/y/z momentum_x/y/z
```

`git_commit` comes from `git describe --always --dirty` at build time. A value
ending in `-dirty` means the binary was built from uncommitted changes and must
not be used for reported results.

## 10. Final state and cross-backend comparison

`--output FILE` writes a `# nbody_state ...` header line, then one line per
particle in index order: `x y z vx vy vz` in `%.17g`. Compare a backend with
the serial reference:

```sh
./build/nbody_serial --n 1000 --steps 100 --output ref.txt
./build/nbody_openmp --n 1000 --steps 100 --output omp.txt
python3 scripts/compare_states.py ref.txt omp.txt     # exit 0 = PASS
```

Default tolerance: `|cand - ref| <= 1e-10 + 1e-8 * |ref|` for each value.
Changing the summation order (`-O3 -march=native -ffast-math`) moved the
state by at most 3e-16 after 100 steps and 6e-14 after 1000 steps at
`n = 1000`, so the default tolerance leaves several orders of magnitude of
margin. Do not expect bitwise equality. Differences grow over long runs
because the system is chaotic, so compare at the default step count. Energy
and momentum are extra checks; they do not replace this comparison.

## 11. Backend contracts

**OpenMP.** Parallelize over target particles in the force kernel and,
optionally, kick/drift. No shared accumulators between threads. Report
`omp_threads` from `omp_get_max_threads()`, and record `OMP_PROC_BIND` and
`OMP_PLACES` in job logs.

**Hybrid MPI+OpenMP.**
- Every rank calls `particles_init_cube` with the same seed. This gives
  identical full arrays on each rank with no broadcast.
- Rank `r` of `P` owns `partition_range(n, P, r, &begin, &end)`. Its velocities
  and accelerations are authoritative only in that range.
- Each rank calls `compute_accelerations_openmp(p, begin, end, G, eps)` and
  kicks and drifts only `[begin, end)`.
- After each drift, positions are synchronized before the next force
  evaluation with `MPI_Allgatherv` (counts and displacements from
  `partition_range`). Masses never change and are not exchanged. Three calls,
  one per coordinate array, is the baseline. Packing into one buffer is a
  candidate optimization, so record which variant was measured.
- Communication time goes in `communication_seconds`.
- For `--check` and `--output`, gather the owned velocities (and use the
  already-replicated positions) on rank 0. Only rank 0 prints or writes.

**OpenACC.** The same CLI, result line and state file apply. The arrays may
stay on the device across steps. Report end-to-end time; host-device transfer
time goes in `communication_seconds`, and kernel time goes in
`force_seconds`. Set `gpu_model` (for example from `nvidia-smi`).

## 12. Build and run conventions

- C11, `-O2`, `-Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Werror`.
  `make WERROR=` turns off `-Werror` for a first build with a new compiler.
- Build on the cluster after `. env/modules.sh`; do not copy binaries from a
  laptop.
- Job logs go to `results/logs/`. Root-level `*.out` files are git-ignored.
