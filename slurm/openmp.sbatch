#!/bin/bash
#SBATCH --job-name=nbody-openmp
#SBATCH --output=results/logs/openmp_%j.out
#SBATCH --partition=cpu
#SBATCH --time=00:30:00
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --hint=nomultithread
#SBATCH --mem=4G
#
# OpenMP thread-scaling sweep on one node: one --check run with the most
# threads, then REPS timed runs for every thread count in THREADS.
# All runs share one allocation, so every thread count runs on the same node.
# Submit from the repository root after building:
#     . env/modules.sh && make && sbatch slurm/openmp.sbatch
# Override the workload without editing the script, e.g.
#     sbatch --export=ALL,N=5000,THREADS="1 2 4" slurm/openmp.sbatch
# The largest value in THREADS must not exceed --cpus-per-task.
# The student QoS allows 64 CPUs per user; with --hint=nomultithread each core
# counts as 2 CPUs, so 32 cores is the maximum.

cd "${SLURM_SUBMIT_DIR:?run this script with sbatch}"

# Load modules before `set -u`; module scripts may read unset variables.
. env/modules.sh
set -euo pipefail

N="${N:-20000}"
STEPS="${STEPS:-20}"
DT="${DT:-0.001}"
EPS="${EPS:-0.01}"
SEED="${SEED:-42}"
REPS="${REPS:-3}"
THREADS="${THREADS:-1 2 4 8 16 32}"
ARGS=(--n "$N" --steps "$STEPS" --dt "$DT" --eps "$EPS" --seed "$SEED")

# One thread per physical core, threads packed next to each other.
export OMP_PLACES=cores
export OMP_PROC_BIND=close

if [ ! -x build/nbody_openmp ]; then
    echo "build/nbody_openmp not found; run 'make' first" >&2
    exit 1
fi

sh scripts/job_metadata.sh
echo "threads_list=$THREADS"
echo "command=./build/nbody_openmp ${ARGS[*]}"

MAX_THREADS=$(echo "$THREADS" | tr ' ' '\n' | sort -n | tail -n 1)

echo "=== check run threads=$MAX_THREADS"
OMP_NUM_THREADS=$MAX_THREADS srun --ntasks=1 --cpus-per-task="$MAX_THREADS" \
    --hint=nomultithread ./build/nbody_openmp "${ARGS[@]}" --check

for t in $THREADS; do
    for rep in $(seq 1 "$REPS"); do
        echo "=== timed run threads=$t rep=$rep"
        OMP_NUM_THREADS=$t srun --ntasks=1 --cpus-per-task="$t" \
            --hint=nomultithread ./build/nbody_openmp "${ARGS[@]}"
    done
done

sacct -j "$SLURM_JOB_ID" --format=JobID,JobName,Elapsed,TotalCPU,AllocCPUS,MaxRSS,NodeList,State 2>/dev/null || true




