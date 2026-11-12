# Reproducing the experiments

## Environment

| Item | Value |
|---|---|
| Cluster | SciTech, nodes `haskell`, `fortran` and `julia`: each 2 × AMD EPYC 9755 (128 cores each, SMT 2, 512 logical CPUs, 2 NUMA nodes). Partitions `cpu` (default), `gpu` and `interactive` all span the three nodes. Per-user limit 64 CPUs = 32 physical cores with `--hint=nomultithread` |
| Modules | `env/modules.sh`: `OpenMPI/4.1.5-GCC-12.3.0` (loads `GCC/12.3.0`; `mpicc` wraps the same gcc 12.3.0) for serial, OpenMP and hybrid. `env/modules_openacc.sh`: `nvhpc/25.7` (`nvc`, CUDA 12.9) for OpenACC only |
| GPUs | haskell: 2 × RTX 6000 Ada (cc89); fortran: 2 × and julia: 1 × RTX PRO 6000 Blackwell (cc120). Request with `--partition=gpu --gres=gpu:1` |
| Login node | `rust`, a QEMU virtual CPU without AVX: do not build with `-march=native` or `-gpu=ccnative` there |
| Compiler flags | `-std=c11 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Werror`, plus `-fopenmp` for OpenMP and hybrid. OpenACC: the same without `-Werror`, plus `-acc -gpu=cc89,cc120 -Minfo=accel`. Every binary prints its real flags in `build_flags` |
| MPI transport | Since `cf07216` (#10) `slurm/hybrid.sbatch` sets `OMPI_MCA_pml=ob1`, `OMPI_MCA_btl=self,vader,tcp` and `OMPI_MCA_btl_tcp_if_include=10.205.20.0/24`. Runs before that used Open MPI's default (UCX) |
| OpenMP placement | `OMP_PLACES=cores`, `OMP_PROC_BIND=close`, set in the job scripts and printed in each log's metadata |
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
make            # nbody_serial, nbody_openmp, nbody_hybrid
make test       # unit + CLI tests; one core, no MPI or GPU needed
```

OpenACC (separate environment, run on a GPU node):

```sh
. env/modules_openacc.sh
make openacc    # build/nbody_openacc for cc89 and cc120
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

## Serial problem-size sweep

```sh
for n in 2000 5000 10000 20000; do
  sbatch --time=00:20:00 --export=ALL,N=$n slurm/serial.sbatch
done
sbatch --time=00:40:00 --export=ALL,N=100000,STEPS=10 slurm/serial.sbatch
```

All sweep binaries were built from `0f39c80938b5`. The job logs show
`git_head=6d94fd8`, a later commit that only added a log file, so the code is
identical. Collect the runs with:

```sh
python3 scripts/logs_to_csv.py results/logs/serial_*.out \
    --csv results/serial_runs.csv --summary results/serial_summary.md
```

Medians of 3 timed repetitions (spread = (max − min) / median):

| N | steps | node | job | median s/step | spread | median interactions/s |
|---:|---:|---|---:|---:|---:|---:|
| 1,000 | 100 | haskell | 28010 | 3.0747e-3 | 0.2% | 3.249e8 |
| 2,000 | 100 | fortran | 28024 | 1.2246e-2 | 0.1% | 3.265e8 |
| 5,000 | 100 | fortran | 28025 | 7.6528e-2 | 1.3% | 3.266e8 |
| 10,000 | 100 | fortran | 28026 | 0.30634 | 5.1% | 3.264e8 |
| 20,000 | 100 | fortran | 28027 | 1.2244 | 0.1% | 3.267e8 |
| 100,000 | 10 | fortran | 28028 | 30.716 | 0.1% | 3.256e8 |

Findings:

- Time per step grows as N²: doubling N multiplies it by 3.997–4.003 (5k→10k,
  10k→20k). `interactions_per_second` stays within 0.4% of 3.26e8 from 2k to
  100k on `fortran`.
- No cache or memory effect appears up to N = 100,000. The four arrays the
  inner loop reads take 3.2 MB at that size, which fits in cache. The kernel is
  compute-bound, so its scalar arithmetic speed is the bottleneck (the SIMD
  optimization target).
- The two large spreads come from one slow repetition each: N = 10,000 rep 3
  is 5% slower and N = 5,000 rep 3 is 1.3% slower. The other repetitions agree
  within 0.1%. Report medians; the nodes are shared, so occasional
  interference is expected.
- `haskell` (N = 1,000) and `fortran` differ by 0.5% in interactions/s.
- The relative energy change over 100 steps stays below 1.2e-8 for every N,
  and every momentum component stays below 1.3e-18.

Local development checks used Debian GCC 12.2.0 (WSL). They are not reported
results.

## CPU scaling experiments (E2–E5)

Build on the login node with `. env/modules.sh && make`, then submit from the
repository root. Each job runs one `--check` run and three timed repetitions
per configuration inside one allocation, so every configuration of a job runs
on the same node(s).

| ID | Question | Command |
|---|---|---|
| E2 | OpenMP threads, N = 20,000, 20 steps | `sbatch slurm/openmp.sbatch` (defaults: `THREADS="1 2 4 8 16 32"`, 32 cores) |
| E3 | Pure MPI, N = 100,000, 10 steps | `sbatch --export=ALL,SPLITS="2x1 4x1 8x1 16x1 32x1" slurm/hybrid.sbatch` |
| E4 | Ranks × threads on 32 cores, one node | `sbatch slurm/hybrid.sbatch` (defaults: `SPLITS="1x32 2x16 4x8 8x4 16x2 32x1"`) |
| E5 | The E4 splits on two nodes, 16 + 16 cores | `sbatch --nodes=2 --ntasks-per-node=16 --export=ALL,SPLITS="2x16 4x8 8x4 16x2 32x1" slurm/hybrid.sbatch` |

Recorded runs. All binaries were built from `19f3375be0f7`
(`git_commit` in each result line); `git_head` is the checkout the job script
ran from.

| ID | Job | Node(s) | `git_head` | Status | Log |
|---|---:|---|---|---|---|
| E2 | 28604 | `haskell` | `19f3375` | Completed | `results/logs/openmp_28604.out` |
| E3 | 28607 | `fortran` | `19f3375` | Completed | `results/logs/hybrid_28607.out` |
| E4 | 28605 | `fortran` | `19f3375` | Completed | `results/logs/hybrid_28605.out` |
| E5 | 28606 | `fortran`, `haskell` | `19f3375` | Cancelled: Open MPI tried the second network (10.205.10.0/24) between nodes; fixed in `5a30305`. Not used | `results/logs/hybrid_28606.out` |
| E5 | 28660 | `fortran`, `julia` | `5a30305` | Completed, **not comparable with E4**: the log shows `omp_proc_bind=unset` (export bug in `5a30305`, fixed in #10), and it ran on UCX. Rerun on `cf07216` or later | `results/logs/hybrid_28660.out` |

Collect the runs into the committed CSV and summary (this regenerates
`results/scaling_summary.md` exactly):

```sh
python3 scripts/logs_to_csv.py results/logs/openmp_28604.out \
    results/logs/hybrid_28605.out results/logs/hybrid_28607.out \
    results/logs/hybrid_28660.out \
    --csv results/scaling_runs.csv --summary results/scaling_summary.md
```

Speedups use the serial times above as the baseline (N = 20,000: job 28027;
N = 100,000: job 28028). `results/scaling_summary.md` has no nodes column: the
1-node and 2-node rows differ only in the job ID.

## Hybrid correctness on the cluster

```sh
. env/modules.sh && make && sbatch slurm/validate_hybrid.sbatch
```

Job 28692 (`git_head` `3cf8754`, nodes `fortran` and `haskell`) compares
`nbody_hybrid` with `nbody_serial` through `scripts/compare_states.py`
(N = 1,000, 100 steps): 1, 2, 3, 4, 7 and 8 ranks on one node; 2, 3, 8 and 16
ranks on two nodes; and 2, 4 and 8 ranks × 2 threads on two nodes. All PASS. Log: `results/logs/validate_hybrid_28692.out`.

## OpenACC (E6, in progress)

The job builds `nbody_openacc` on the GPU node and compares it with
`nbody_serial`, so build the serial reference first:

```sh
. env/modules.sh && make
. env/modules_openacc.sh
sbatch slurm/openacc.sbatch                              # any GPU
sbatch --gres=gpu:rtx6000ada:1 slurm/openacc.sbatch      # pin Ada (cc89)
sbatch --export=ALL,N=20000,STEPS=20 slurm/openacc.sbatch
```

Run the OpenACC commands in their own login shell. `module purge` does not
undo nvhpc's `PATH` change, so `. env/modules.sh` afterwards in the same shell
links with `/usr/bin/ld` and the hybrid build fails with `undefined reference
to hwloc_...` (`env/modules.sh` now warns about this).

Load `env/modules_openacc.sh` before `sbatch` while `slurm/openacc.sbatch`
starts with plain `#!/bin/bash`: the job does not load modules itself yet
(job 28623 logs `module command not available`).

| Job | Node / GPU | `git_head` | N, steps | Result | Log |
|---:|---|---|---|---|---|
| 28623 | `haskell`, RTX 6000 Ada | `a302966` | 1,000, 100 | Kernel tests and comparison with serial PASS; median 0.78 ms/step (3.9× serial) | `results/logs/openacc_28623.out` |
