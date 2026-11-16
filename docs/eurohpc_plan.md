# EuroHPC scale-up plan (draft)

Owners: Matheus, Fernando. Status: draft v1, 15 Nov (E6 added). Numbers
marked "after E8" will be updated when the SIMD results land.

## 1. Why a larger system

The force calculation is direct all-pairs, so one step costs about N²
interactions. Our measured rate is 3.26 × 10⁸ interactions/s per core (serial,
job 28028), and pure MPI keeps 98% of that on 32 cores (E3, job 28860:
1.02 × 10¹⁰ interactions/s). Our per-user limit on the teaching cluster is
32 cores, so:

| N | Interactions per step | 32 cores (measured rate) | 1,000 steps |
|---:|---:|---:|---:|
| 100,000 | 10¹⁰ | 0.98 s | 16 min |
| 1,000,000 | 10¹² | 98 s | 27 h |
| 10,000,000 | 10¹⁴ | 2.7 h | 113 days |

Systems of 10⁶–10⁷ bodies (star clusters, small galaxies) are where direct
N-body is scientifically useful and out of reach here. A 10× larger N needs
100× more compute, so the only way forward is many nodes.

## 2. Target system

**CPU: LUMI-C** (2,048 nodes; 2 × AMD EPYC 7763, 128 cores and 256 GiB per
node; Slingshot-11 at 200 Gb/s). It is the closest match to our development
nodes (AMD EPYC, x86, Slurm), so the MPI + OpenMP code and job scripts port
with few changes.

**GPU: Leonardo Booster** (3,456 nodes; 4 × NVIDIA A100 64 GB per node; HDR
InfiniBand 200 Gb/s). E6 showed the GPU is worth it above N ≈ 10,000: one
RTX 6000 Ada was 2.5× faster than 32 CPU cores of the same node at
N = 100,000 (0.407 vs 1.022 s/step, 75× serial), with host–device transfers at
0.05% of the run [jobs 28906, 29841]. Our kernel is double precision, and the
RTX 6000 Ada is a workstation card whose FP64 rate is a small fraction of its
FP32 rate; the A100 is a data-centre card with a 9.7 TFLOPS FP64 peak, so we
expect the GPU's lead to grow there. Our OpenACC backend is built with
NVIDIA's `nvc`, so it moves to Leonardo by adding `-gpu=cc80`. LUMI-G's AMD
MI250X GPUs are also strong in FP64 but would need a different compiler path.

## 3. Requirements and estimates

Assumptions: rate per LUMI-C core equal to ours (to be confirmed in step A
below, since the 7763 is an older generation than our EPYC 9755), 95% parallel
efficiency, 128 cores per node, so about 4 × 10¹⁰ interactions/s per node.

- **Time per step:** N = 1M: 25 s on 1 node. N = 10M: 2,525 s on 1 node,
  39 s on 64 nodes, 10 s on 256 nodes.
- **Memory:** every process holds all particles, about 80 bytes each (our
  MaxRSS is 8–9.5 MB at N = 100,000). N = 10M is 0.8 GB per process: fine with
  1–2 ranks per node, 102 GB per node with 128 pure MPI ranks. N = 100M would
  need 1 TB per node with pure MPI, so large N requires the hybrid layout.
- **Nodes:** 8–256 for scaling tests, 64 for production.
- **Run duration:** scaling runs a few minutes to 2 h; the production run
  (N = 10M, 1,000 steps, 64 nodes) about 11 h.

| Step | What | Runs | Node-hours |
|---|---|---|---:|
| A | Port, build, validate against serial, measure the per-core rate | ~10 short | 10 |
| B | Ranks × threads splits on one node, N = 1M | 6 splits × 3 reps | 5 |
| C | Strong scaling, N = 10M, 5 steps, 8–256 nodes, 2 layouts | 6 × 2 × 3 = 36 | 126 |
| D | Weak scaling, N = 1M–16M on 1–256 nodes, 2 layouts | 5 × 2 × 3 = 30 | 72 |
| E | Production, N = 10M, 1,000 steps, 64 nodes | 1 | 702 |
| | Subtotal | | 915 |
| | With 20% margin for failed and repeated jobs | | **≈ 1,100** |

That is about 140,000 core-hours on LUMI-C. If the SIMD kernel (E8) speeds up
each core by a factor k, every CPU figure above divides by k.

**GPU request (Leonardo Booster).** Our OpenACC code uses one GPU, so running
on many GPUs needs an MPI + OpenACC version (one rank per GPU, the same
`MPI_Allgatherv` as the hybrid code): that is new work, done in step G2.
Estimates use the rate we measured on the RTX 6000 Ada, 2.46 × 10¹⁰
interactions/s per GPU at N = 100,000 (about 10¹¹ per 4-GPU node), so they
are conservative for the A100.

| Step | What | Runs | Node-hours |
|---|---|---|---:|
| G1 | Port to A100, validate, single-GPU N-sweep up to N = 1M | ~15 short | 5 |
| G2 | Develop and test MPI + OpenACC, one rank per GPU | debugging | 20 |
| G3 | Strong scaling, N = 10M, 5 steps, 1–8 nodes (4–32 GPUs) | 4 × 3 = 12 | 17 |
| | With 20% margin | | **≈ 50** |

If G3 scales, a 4-GPU node does about 2.5× the work of a LUMI-C node, so the
production run (N = 10M, 1,000 steps) would need about 280 Leonardo node-hours
on 16 nodes instead of 702 on LUMI-C.

**Access route:** a Benchmark Access request (scalability tests, up to three
months, monthly cut-offs) for steps A–D (about 215 node-hours), then Regular
Access for production, using the step C and D results as evidence.

## 4. Software

C11, GCC (or the Cray/AMD compilers), any MPI-3 library (we use Open MPI
4.1.5 and only standard collectives, so Cray MPICH works), OpenMP, Slurm,
Python 3 with matplotlib for the analysis scripts. GPU: NVIDIA HPC SDK
(`nvc`, OpenACC). Everything else is in this repository.

## 5. Expected scaling challenges

1. **All-to-all communication.** Every rank receives all N positions each
   step (24 N bytes, three `MPI_Allgatherv` calls). Measured communication
   per step at N = 100,000 (median of 3; jobs 28858 and 28859):

   | Ranks per node | 1 node (32 cores) | 2 nodes (16 + 16 cores) |
   |---:|---:|---:|
   | 1 | 0 ms (0%) | 35 ms (3.2%) |
   | 2 | 8 ms (0.8%) | 113 ms (9.7%) |
   | 4 | 19 ms (1.8%) | 137 ms (11.8%) |
   | 8 | 26 ms (2.5%) | 180 ms (15.3%) |
   | 16 | 59 ms (5.5%) | 124 ms (11.4%) |
   | 32 | 14 ms (1.4%) | – |

   One rank per node is clearly cheapest between nodes. Beyond that the cost
   does not grow with the bytes sent (16 ranks per node cost less than 8), so
   it is not bandwidth alone. Two limits of these numbers: the reported time
   is the maximum over ranks and includes time spent waiting inside
   `MPI_Allgatherv` for slower ranks, not only transfer; and our nodes use
   Open MPI over TCP (UCX crashed at 16 ranks on 2 nodes, jobs 28689–28690),
   not a high-speed fabric. Even the best case is slow: at one rank per node
   each rank receives 1.2 MB from the other node in 35 ms, about 34 MB/s.
   At scale we would therefore run 1–2 ranks per node with threads, and test
   packing x/y/z into one call and overlapping communication with
   computation (ring pipeline). We did not implement packing on our cluster:
   with these numbers we cannot tell whether it would help, so step A
   measures transfer and waiting separately (a barrier before the gather).
2. **Compute/communication ratio in strong scaling.** Compute per node falls
   as N²/P but each node still receives 24 N bytes, so efficiency drops at
   high node counts and small N. At N = 10M each node receives 240 MB per
   step: about 10 ms at Slingshot's 200 Gb/s against a 10–40 s step. At the
   rate we achieved over TCP (34 MB/s) it would be about 7 s, no longer
   negligible, so the achieved bandwidth on LUMI-C is measured in step A
   before the step C and D runs are submitted.
3. **Threads vs ranks.** Pure MPI was 8% faster than one rank with 32
   threads (E4, E7), but the hybrid layout is the one that scales in memory
   and communication. Understanding that gap (E7) matters more at scale.
4. **Faster compute makes communication matter more.** The SIMD kernel (E8,
   PR #21, 1.94× per core on haskell) is so far used only by the OpenMP
   backend. If the hybrid backend uses it, compute halves while communication
   does not change, so on two nodes communication would rise from 3–15% to
   about 6–26% of a step (same measurements as item 1). The same code will
   also run on a different CPU generation, so the per-core rate must be
   measured again before production.

## 6. Support needed

Help with the Cray programming environment on LUMI (compiler and MPI modules,
process and thread binding), access to profilers we do not have on our
cluster (perf, CrayPat) to confirm the E7 explanation, and, for the GPU part,
advice on running OpenACC with multiple GPUs per node on Leonardo.

## Sources

- LUMI-C hardware: https://docs.lumi-supercomputer.eu/hardware/lumic/
- Leonardo hardware: https://docs.hpc.cineca.it/hpc/leonardo.html
- A100 FP64 peak (9.7 TFLOPS): https://getdeploying.com/gpus/nvidia-a100-vs-nvidia-rtx-6000-ada
- E6 results: `results/e6_crossover.csv`, jobs 28906 and 29841
- EuroHPC Benchmark Access: https://eurohpc-ju.europa.eu/eurohpc-ju-call-proposals-benchmark-access_en
