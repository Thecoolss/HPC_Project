#!/bin/bash -l
#SBATCH --job-name=e6-openmp-sweep
#SBATCH --output=results/logs/e6_openmp_%j.out
#SBATCH --time=00:30:00
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --hint=nomultithread
#SBATCH --nodelist=haskell
#SBATCH --mem=8G
#
# CPU baseline for E6: OpenMP-32 across the same N as the GPU sweep.
# Must match the GPU node and use real cores, or the crossover is meaningless
# (Sami/Athul review on #14): --nodelist=haskell (same node as the Ada sweep),
# --hint=nomultithread (one thread per physical core), and --cpus-per-task on
# srun (newer Slurm does not propagate the batch allocation to the step).
# Sanity: N=20k should land near 0.039 s/step, not 0.077.

cd "${SLURM_SUBMIT_DIR:?run this script with sbatch}"

. env/modules.sh
set -euo pipefail

export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-32}"
export OMP_PROC_BIND=close
export OMP_PLACES=cores

NS=(1000 10000 20000 100000)
STEPSES=(100 100 100 10)
DT="${DT:-0.001}"
EPS="${EPS:-0.01}"
SEED="${SEED:-42}"
REPS="${REPS:-3}"

if [ ! -x build/nbody_openmp ]; then
    echo "build/nbody_openmp not found; run '. env/modules.sh && make' first" >&2
    exit 1
fi

sh scripts/job_metadata.sh
echo "OMP_NUM_THREADS=$OMP_NUM_THREADS"

for i in "${!NS[@]}"; do
    N="${NS[$i]}"
    STEPS="${STEPSES[$i]}"
    ARGS=(--n "$N" --steps "$STEPS" --dt "$DT" --eps "$EPS" --seed "$SEED")
    echo "############ N=$N STEPS=$STEPS threads=$OMP_NUM_THREADS ############"
    echo "command=./build/nbody_openmp ${ARGS[*]}"
    for rep in $(seq 1 "$REPS"); do
        echo "=== timed run N=$N rep=$rep"
        srun --ntasks=1 --cpus-per-task="${SLURM_CPUS_PER_TASK}" --hint=nomultithread \
            ./build/nbody_openmp "${ARGS[@]}"
    done
done

sacct -j "$SLURM_JOB_ID" --format=JobID,JobName,Elapsed,TotalCPU,MaxRSS,NodeList,State 2>/dev/null || true
