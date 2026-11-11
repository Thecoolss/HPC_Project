#!/bin/bash
#SBATCH --job-name=acc-probe
#SBATCH --output=results/logs/openacc_probe_%j.out
#SBATCH --time=00:05:00
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=2
#SBATCH --mem=2G
#SBATCH --gres=gpu:1
#
# Day 1 OpenACC toolchain probe (Member D). Confirms, on a real GPU node:
#   1. which GPU we land on and its compute capability,
#   2. that nvc compiles an OpenACC kernel for cc89 + cc120,
#   3. that the kernel actually runs on the device (not a silent host fallback).
# Submit from the repository root (add --partition=<gpu partition> if needed):
#     sbatch slurm/openacc_probe.sbatch
#     sbatch --partition=gpu slurm/openacc_probe.sbatch

cd "${SLURM_SUBMIT_DIR:?run this script with sbatch}"
module load nvhpc/25.7
set -euo pipefail

echo "=== node";    hostname
echo "=== gpu";     nvidia-smi --query-gpu=name,compute_cap,memory.total,driver_version --format=csv
echo "=== CUDA_VISIBLE_DEVICES=${CUDA_VISIBLE_DEVICES:-unset}"
echo "=== nvc";     nvc --version | head -2
echo "=== nvaccelinfo (summary)"; nvaccelinfo 2>/dev/null | grep -E 'Device Name|Device Revision|Default Target' || true

BIN="$(mktemp -d)/acc_smoke"
echo "=== compile"
nvc -std=c11 -O2 -acc -gpu=cc89,cc120 -Minfo=accel \
    tests/acc_smoke/acc_smoke.c -o "$BIN" -lm

echo "=== run (NVCOMPILER_ACC_NOTIFY=1 prints each kernel launch)"
NVCOMPILER_ACC_NOTIFY=1 "$BIN"
