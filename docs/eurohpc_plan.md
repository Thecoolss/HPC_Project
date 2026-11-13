# EuroHPC scale-up plan (draft)

Owners: Matheus, Fernando. Status: draft v0, 13 Nov. Numbers marked
"after E6" or "after E8" will be updated when those experiments land.

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

**GPU comparison (after E6): Leonardo Booster** (3,456 nodes; 4 × NVIDIA A100
64 GB per node; HDR InfiniBand 200 Gb/s). Our OpenACC backend is built with
NVIDIA's `nvc`, so it moves there by changing `-gpu=cc89,cc120` to
`-gpu=cc80`. LUMI-G's AMD GPUs would need a different compiler path.

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

That is about 140,000 core-hours on LUMI-C. A GPU request for Leonardo is
added after E6 shows where the GPU beats the CPU. If the SIMD kernel (E8)
speeds up each core by a factor k, every compute figure above divides by k.

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
   step (24 N bytes, three `MPI_Allgatherv` calls). On our cluster
   communication was 0–5.5% of a step on one node but 3–15% on two nodes
   (E5, job 28859), and lowest with one rank per node (2 × 16: 35 ms vs
   113–180 ms). Per-node traffic grows with the number of ranks per node, so
   at scale we would run 1–2 ranks per node with threads, pack x/y/z into one
   call, and test overlapping communication with computation (ring pipeline).
2. **Compute/communication ratio in strong scaling.** Compute per node falls
   as N²/P but each node still receives 24 N bytes, so efficiency drops at
   high node counts and small N. At N = 10M this stays small (240 MB per node
   per step at 200 Gb/s is about 10 ms against a 10–40 s step).
3. **Threads vs ranks.** Pure MPI was 8% faster than one rank with 32
   threads (E4, E7), but the hybrid layout is the one that scales in memory
   and communication. Understanding that gap (E7) matters more at scale.
4. **Per-core efficiency.** The force loop is not vectorized (E7). The same
   code will run on a different CPU generation, so the per-core rate must be
   measured again before production.

## 6. Support needed

Help with the Cray programming environment on LUMI (compiler and MPI modules,
process and thread binding), access to profilers we do not have on our
cluster (perf, CrayPat) to confirm the E7 explanation, and, for the GPU part,
advice on running OpenACC with multiple GPUs per node on Leonardo.

## Sources

- LUMI-C hardware: https://docs.lumi-supercomputer.eu/hardware/lumic/
- Leonardo hardware: https://docs.hpc.cineca.it/hpc/leonardo.html
- EuroHPC Benchmark Access: https://eurohpc-ju.europa.eu/eurohpc-ju-call-proposals-benchmark-access_en
