#!/bin/bash
#SBATCH --job-name=nbody-serial
#SBATCH --output=results/logs/serial_%j.out
#SBATCH --time=00:10:00
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=1G
#
# Serial baseline: one correctness run (--check) and REPS timed runs.
# Submit from the repository root after building with the same modules:
#     . env/modules.sh && make test && sbatch slurm/serial.sbatch
# Override the workload without editing the script, e.g.
#     sbatch --time=00:30:00 --export=ALL,N=10000 slurm/serial.sbatch
# Serial cost grows as N^2: about 8 ms/step at N=1000 on a desktop CPU, so
# raise --time for larger N.

cd "${SLURM_SUBMIT_DIR:?run this script with sbatch}"

# Load modules before `set -u`; module scripts may read unset variables.
. env/modules.sh
set -euo pipefail

N="${N:-1000}"
STEPS="${STEPS:-100}"
DT="${DT:-0.001}"
EPS="${EPS:-0.01}"
SEED="${SEED:-42}"
REPS="${REPS:-3}"
ARGS=(--n "$N" --steps "$STEPS" --dt "$DT" --eps "$EPS" --seed "$SEED")

if [ ! -x build/nbody_serial ]; then
    echo "build/nbody_serial not found; run 'make' first" >&2
    exit 1
fi

sh scripts/job_metadata.sh
echo "command=./build/nbody_serial ${ARGS[*]}"

echo "=== check run"
srun ./build/nbody_serial "${ARGS[@]}" --check

for rep in $(seq 1 "$REPS"); do
    echo "=== timed run rep=$rep"
    srun ./build/nbody_serial "${ARGS[@]}"
done

sacct -j "$SLURM_JOB_ID" --format=JobID,JobName,Elapsed,TotalCPU,MaxRSS,NodeList,State 2>/dev/null || true
