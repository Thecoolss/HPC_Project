#!/bin/bash -l
#SBATCH --job-name=e6-openacc-sweep
#SBATCH --output=results/logs/e6_openacc_%j.out
#SBATCH --time=00:30:00
#SBATCH --partition=gpu
#SBATCH --gres=gpu:rtx6000ada:1
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=2
#SBATCH --mem=8G
#
# E6 -- "When is the GPU worth it?" OpenACC N-sweep on ONE Ada card.
# For each N it runs one --check (correctness + energy conservation) and REPS
# timed runs. Every timed run prints one `backend=...` line that
# scripts/logs_to_csv.py parses, so one log file yields the whole sweep.
#
# Build the serial reference first on the login node so --check can compare:
#     . env/modules.sh && make            # builds build/nbody_serial
#     sbatch slurm/e6_openacc_sweep.sbatch
#
# Pin the card explicitly: this gres lands on haskell (RTX 6000 Ada, cc89).
# If the gres type name is rejected, fall back to:
#     #SBATCH --gres=gpu:1
#     #SBATCH --nodelist=haskell
# For the Blackwell card instead, use a fortran/julia node (cc120); out of
# scope for the minimal E6, but the binary already targets cc89,cc120.

cd "${SLURM_SUBMIT_DIR:?run this script with sbatch}"

. env/modules_openacc.sh
set -euo pipefail

# N values and matched step counts (same steps as the serial E1 baseline, so
# seconds_per_step compares directly against results/serial_runs.csv).
NS=(1000 10000 20000 100000)
STEPSES=(100 100 100 10)
DT="${DT:-0.001}"
EPS="${EPS:-0.01}"
SEED="${SEED:-42}"
REPS="${REPS:-3}"

sh scripts/job_metadata.sh
nvidia-smi --query-gpu=name,compute_cap,memory.total,driver_version --format=csv

echo "=== build"
make openacc build/test_openacc

echo "=== kernel correctness tests"
srun ./build/test_openacc

STATE_DIR="$(mktemp -d)"

for i in "${!NS[@]}"; do
    N="${NS[$i]}"
    STEPS="${STEPSES[$i]}"
    ARGS=(--n "$N" --steps "$STEPS" --dt "$DT" --eps "$EPS" --seed "$SEED")
    echo "############ N=$N STEPS=$STEPS ############"
    echo "command=./build/nbody_openacc ${ARGS[*]}"

    echo "=== check run N=$N (NVCOMPILER_ACC_TIME=1: per-kernel + transfer times)"
    NVCOMPILER_ACC_TIME=1 srun ./build/nbody_openacc "${ARGS[@]}" --check \
        --output "$STATE_DIR/openacc_${N}.txt"

    if [ -x build/nbody_serial ]; then
        echo "=== compare final state with serial N=$N"
        srun ./build/nbody_serial "${ARGS[@]}" --output "$STATE_DIR/serial_${N}.txt" >/dev/null
        python3 scripts/compare_states.py "$STATE_DIR/serial_${N}.txt" "$STATE_DIR/openacc_${N}.txt"
    else
        echo "=== compare skipped N=$N: build/nbody_serial missing (run 'make' on the login node)"
    fi

    for rep in $(seq 1 "$REPS"); do
        echo "=== timed run N=$N rep=$rep"
        srun ./build/nbody_openacc "${ARGS[@]}"
    done
done

sacct -j "$SLURM_JOB_ID" --format=JobID,JobName,Elapsed,TotalCPU,MaxRSS,NodeList,State 2>/dev/null || true
