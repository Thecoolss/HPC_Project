#!/bin/bash -l
#SBATCH --job-name=nbody-openacc
#SBATCH --output=results/logs/openacc_%j.out
#SBATCH --time=00:15:00
#SBATCH --partition=gpu
#SBATCH --gres=gpu:1
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=2
#SBATCH --mem=4G
#
# OpenACC backend: builds with NVHPC on the GPU node, runs the kernel tests,
# one --check run (with the NVHPC per-kernel time summary), a comparison of
# the final state against the serial reference, and REPS timed runs.
#
# Build the serial reference first on the login node (GCC module):
#     . env/modules.sh && make
#     sbatch slurm/openacc.sbatch
# Pin a GPU generation or change the workload without editing the script:
#     sbatch --gres=gpu:rtx6000ada:1 slurm/openacc.sbatch
#     sbatch --export=ALL,N=20000,STEPS=20 slurm/openacc.sbatch

cd "${SLURM_SUBMIT_DIR:?run this script with sbatch}"

# Load modules before `set -u`; module scripts may read unset variables.
# env/modules_openacc.sh purges first and loads nvhpc on its own, so the
# NVHPC toolchain is never mixed with the EESSI GCC/OpenMPI modules (Sami review).
. env/modules_openacc.sh
set -euo pipefail

N="${N:-1000}"
STEPS="${STEPS:-100}"
DT="${DT:-0.001}"
EPS="${EPS:-0.01}"
SEED="${SEED:-42}"
REPS="${REPS:-3}"
ARGS=(--n "$N" --steps "$STEPS" --dt "$DT" --eps "$EPS" --seed "$SEED")

sh scripts/job_metadata.sh
nvidia-smi --query-gpu=name,compute_cap,memory.total,driver_version --format=csv

echo "=== build"
make openacc build/test_openacc

echo "=== kernel tests (outside a data region)"
srun ./build/test_openacc

STATE_DIR="$(mktemp -d)"
echo "command=./build/nbody_openacc ${ARGS[*]}"

echo "=== check run (NVCOMPILER_ACC_TIME=1: per-kernel and transfer times)"
NVCOMPILER_ACC_TIME=1 srun ./build/nbody_openacc "${ARGS[@]}" --check \
    --output "$STATE_DIR/openacc.txt"

if [ -x build/nbody_serial ]; then
    echo "=== compare final state with serial"
    srun ./build/nbody_serial "${ARGS[@]}" --output "$STATE_DIR/serial.txt" >/dev/null
    python3 scripts/compare_states.py "$STATE_DIR/serial.txt" "$STATE_DIR/openacc.txt"
else
    echo "=== compare skipped: build/nbody_serial missing (run 'make' on the login node)"
fi

for rep in $(seq 1 "$REPS"); do
    echo "=== timed run rep=$rep"
    srun ./build/nbody_openacc "${ARGS[@]}"
done

sacct -j "$SLURM_JOB_ID" --format=JobID,JobName,Elapsed,TotalCPU,MaxRSS,NodeList,State 2>/dev/null || true
