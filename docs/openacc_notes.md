# OpenACC experiment notes (Member D)

## Day 1 – toolchain and GPU check

Sources: Fernando's Day 1 review (issue #1) and `slurm/openacc_probe.sbatch`.

| Item | Value | Source |
|---|---|---|
| OpenACC compiler | nvhpc/25.7 (`nvc`, CUDA 12.9) | issue #1 |
| GPUs | haskell: 2x RTX 6000 Ada (cc89); fortran: 2x RTX 6000 Blackwell; julia: 1x RTX 6000 Blackwell (cc120) | issue #1 |
| GPU partition / gres syntax | TODO (`sinfo -o "%P %N %G"`) | login node |
| Build flags | `-acc -gpu=cc89,cc120 -Minfo=accel -O2`, no `-Werror` (NVHPC math header warns on `isfinite`) | issue #1 |
| Probe job | TODO job id, node, PASS/FAIL | `results/logs/openacc_probe_<id>.out` |

Implications for the backend:
- Build for both cc89 and cc120, because the node a job lands on is not fixed.
- RTX 6000 cards are workstation GPUs with weak FP64 throughput (a small fraction of
  FP32). The kernel is double precision per the interface, so expect the GPU
  advantage to be smaller than on a datacenter GPU. Measure it; this is a
  finding for the report, not a reason to change precision.
- Compare GPU runs against CPU runs on the same node and record the host.

## Plan (Days 2–10)
- Day 2: `compute_accelerations_openacc` (present-or-copy clauses) +
  `main_openacc.c` with one `acc data` region around the step loop.
- Day 3: validate vs serial with `--output` + `scripts/compare_states.py`.
- Day 4+: timing split (force_seconds = kernels, communication_seconds =
  host-device transfers), 2–3 particle counts vs OpenMP/hybrid.
