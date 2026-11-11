# OpenACC experiment notes (Member D)

## Day 1 – toolchain and GPU check

Sources: Fernando's Day 1 review (issue #1) and `slurm/openacc_probe.sbatch`.

**Result:** one `nvc -acc -gpu=cc89,cc120` build runs correctly on both GPU
generations in the cluster. Day 1 exit criterion met for the OpenACC toolchain.

| Item | Value | Source |
|---|---|---|
| OpenACC compiler | nvhpc/25.7 (`nvc`, CUDA 12.9) | issue #1 |
| GPUs | haskell: 2x RTX 6000 Ada (`gpu:rtx6000ada`, cc89); fortran: 2x and julia: 1x RTX PRO 6000 Blackwell Server Edition (`gpu:rtx6000bb`, cc120, 97887 MiB, driver 580.126.09) | `sinfo`, probe job 28258 |
| GPU partition / gres syntax | `--partition=gpu --gres=gpu:1` (partitions `cpu` (default), `gpu`, `interactive` all span the same 3 nodes; pin a card type with `--gres=gpu:rtx6000ada:1` or `gpu:rtx6000bb:1`) | `sinfo -o "%P %N %G"` |
| Build flags | `-acc -gpu=cc89,cc120 -Minfo=accel -O2`, no `-Werror` (NVHPC math header warns on `isfinite`) | issue #1 |
| Probe job (cc120) | job 28258 on fortran: kernel launched on device 0 (grid=32, block=128), device vs host error 1.4e-15, PASS | `results/logs/openacc_probe_28258.out` |
| Probe job (cc89) | job 28259 pinned with `--gres=gpu:rtx6000ada:1` (RTX 6000 Ada Generation, cc89): kernel launched on device, error 1.4e-15, PASS | `results/logs/openacc_probe_28259.out` |

Implications for the backend:
- Build for both cc89 and cc120, because the node a job lands on is not fixed.
- RTX 6000 cards are workstation GPUs with weak FP64 throughput (a small fraction of
  FP32). The kernel is double precision per the interface, so expect the GPU
  advantage to be smaller than on a datacenter GPU. Measure it; this is a
  finding for the report, not a reason to change precision.
- Compare GPU runs against CPU runs on the same node and record the host.
- Occupancy: with the default `vector_length(128)`, N = 4096 gives only 32
  thread blocks, far fewer than the GPU has SMs. Small N will underuse the
  GPU, so the CPU/GPU crossover is expected at fairly large N; the particle
  count sweep should show where.

## Day 2 – backend implemented

- `src/force_openacc.c`: one GPU thread per target (`parallel loop gang
  vector` over i, `loop seq` over j). `copyin`/`copyout` clauses are
  present-or-copy, so the function also works outside a data region.
- `src/main_openacc.c`: `enter data` before the initial force, `exit data`
  after the last step, so the arrays stay on the GPU for the whole run.
  Kick and drift run on the device (`present` clauses: a missing region is an
  error, never a silent per-step copy). The two transfers are timed as
  `communication_seconds`; `elapsed_seconds` includes them. `acc_init` runs
  before the timers so context creation is not counted.
- `tests/test_openacc.c`: kernel vs serial outside a data region (full range,
  sub-range, untouched targets, empty range).
- `slurm/openacc.sbatch`: build on the GPU node, tests, `--check` run with
  `NVCOMPILER_ACC_TIME=1`, state comparison with serial, `REPS` timed runs.
- CPU-only check (gcc 11 `-fopenacc`, host fallback): kernel tests pass,
  final state matches serial (`compare_states.py` PASS) at n = 1, 2, 1000.
  This proves the logic, not the GPU path; the cluster job does that.

| Item | Result | Evidence |
|---|---|---|
| GPU job | TODO: job id, node, tests, compare, timings | `results/logs/openacc_<id>.out` |

## Plan (Days 2–10)
- Day 2: `compute_accelerations_openacc` (present-or-copy clauses) +
  `main_openacc.c` with one `acc data` region around the step loop.
- Day 3: validate vs serial with `--output` + `scripts/compare_states.py`.
- Day 4+: timing split (force_seconds = kernels, communication_seconds =
  host-device transfers), 2–3 particle counts vs OpenMP/hybrid.
