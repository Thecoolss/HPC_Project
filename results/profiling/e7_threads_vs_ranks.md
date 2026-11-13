# E7: why are OpenMP threads slower than MPI ranks on the same 32 cores?

N = 100,000, 10 steps, node fortran, 32 physical cores (--hint=nomultithread), median of 3.

| Run | Job | s/step | vs pure MPI |
|---|---|---:|---:|
| Pure MPI, 32 ranks x 1 thread | 28858/28860 | 0.979 | - |
| OpenMP, 1 process x 32 threads | 29092 | 1.039 | +6% |
| Hybrid, 1 rank x 32 threads | 28858 | 1.058 | +8% |

Plain OpenMP is also slower, so most of the gap is a threading/memory effect,
not the hybrid code (only ~2 points are hybrid-specific).

OpenMP efficiency drops with N: 98% at N = 20k (E2, job 28604) but 92% at
N = 100k (30.72 / 1.039 = 29.6x). At 20k the arrays read in the force loop
(x, y, z, mass) are 640 KB and fit in each core's 1 MB L2; at 100k they are
3.2 MB and do not. With OpenMP all threads share one copy, rewritten by the
master thread each step and possibly placed on the other socket; MPI ranks
each keep a private copy. Best-supported hypothesis, not directly measured
(no hardware counters on the cluster; cachegrind could confirm).

Job 29092 was built from 9dd27dd; E4 from cf07216. Only Makefile/docs/env
changed in between, not the force code.
