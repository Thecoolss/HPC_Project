#!/bin/sh
# Print reproducibility metadata at the start of a Slurm job log.
# Usage (from the repository root, after sourcing env/modules.sh):
#     sh scripts/job_metadata.sh

echo "=== job metadata ==="
echo "date=$(date -Is)"
echo "host=$(hostname)"
echo "slurm_job_id=${SLURM_JOB_ID:-none}"
echo "slurm_job_name=${SLURM_JOB_NAME:-none}"
echo "slurm_partition=${SLURM_JOB_PARTITION:-none}"
echo "slurm_nodelist=${SLURM_JOB_NODELIST:-none}"
echo "slurm_ntasks=${SLURM_NTASKS:-none}"
echo "slurm_cpus_per_task=${SLURM_CPUS_PER_TASK:-none}"
echo "omp_num_threads=${OMP_NUM_THREADS:-unset}"
echo "omp_proc_bind=${OMP_PROC_BIND:-unset}"
echo "omp_places=${OMP_PLACES:-unset}"
echo "git_head=$(git rev-parse HEAD 2>/dev/null || echo unavailable)"
if [ -n "$(git status --porcelain --untracked-files=no 2>/dev/null)" ]; then
    echo "git_worktree=dirty"
else
    echo "git_worktree=clean"
fi
echo "cc=$(${CC:-cc} --version 2>/dev/null | head -n 1)"
echo "--- modules"
if command -v module >/dev/null 2>&1; then
    module list 2>&1
else
    echo "module command not available"
fi
echo "--- cpu"
lscpu 2>/dev/null | grep -E '^(Model name|Socket|Core|Thread|CPU\(s\)|NUMA node\(s\))' || true
if command -v nvidia-smi >/dev/null 2>&1; then
    echo "--- gpu"
    nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv 2>&1 || true
fi
echo "=== end metadata ==="
