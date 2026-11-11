#!/bin/bash -l
#SBATCH --job-name=nbody-hybrid
#SBATCH --output=results/logs/hybrid_%j.out
#SBATCH --partition=cpu
#SBATCH --time=00:30:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=32
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=8G
#
# Hybrid MPI+OpenMP sweep: the same 32 cores split into different
# ranks x threads combinations. For each split in SPLITS ("RxT" = R MPI ranks,
# T OpenMP threads per rank) it runs REPS timed runs. One --check run uses the
# split with the most ranks. All splits share one allocation (same nodes).
# Submit from the repository root after building:
#     . env/modules.sh && make && sbatch slurm/hybrid.sbatch
# Two nodes, same 32 cores (16 per node); R must be divisible by the node count:
#     sbatch --nodes=2 --ntasks-per-node=16 \
#         --export=ALL,SPLITS="2x16 4x8 8x4 16x2 32x1" slurm/hybrid.sbatch
# Pure MPI strong scaling:
#     sbatch --export=ALL,SPLITS="2x1 4x1 8x1 16x1 32x1" slurm/hybrid.sbatch
# The student QoS allows 64 CPUs per user; with --hint=nomultithread each core
# counts as 2 CPUs, so 32 cores in total is the maximum.

cd "${SLURM_SUBMIT_DIR:?run this script with sbatch}"

# Load modules before `set -u`; module scripts may read unset variables.
. env/modules.sh
set -euo pipefail

N="${N:-100000}"
STEPS="${STEPS:-10}"
DT="${DT:-0.001}"
EPS="${EPS:-0.01}"
SEED="${SEED:-42}"
REPS="${REPS:-3}"
SPLITS="${SPLITS:-1x32 2x16 4x8 8x4 16x2 32x1}"
MPI_PLUGIN="${MPI_PLUGIN:-pmix}"
ARGS=(--n "$N" --steps "$STEPS" --dt "$DT" --eps "$EPS" --seed "$SEED")

NODES="$SLURM_JOB_NUM_NODES"
CORES=$((SLURM_NTASKS * SLURM_CPUS_PER_TASK))

# Threads of each rank pinned to physical cores, packed next to each other.
export OMP_PLACES=cores
export 
OMP_PROC_BIND=close


# Open MPI: use only the main 10.205.20.0/24 network between nodes. Each node
# also has a 10.205.10.0/24 VLAN, and job 28606 failed with TCP connect()
# errors to 10.205.10.x addresses at 16 ranks across 2 nodes.
export OMPI_MCA_btl_tcp_if_include="${OMPI_MCA_btl_tcp_if_include:-10.205.20.0/24}"

if [ ! -x build/nbody_hybrid ]; then
    echo "build/nbody_hybrid not found; run 'make' first" >&2
    exit 1
fi

sh scripts/job_metadata.sh
echo "mpi_version=$(mpirun --version 2>/dev/null | head -n 1)"
echo "mpi_plugin=$MPI_PLUGIN nodes=$NODES cores=$CORES splits_list=$SPLITS"
echo "mpi_tcp_networks=$OMPI_MCA_btl_tcp_if_include"
echo "command=./build/nbody_hybrid ${ARGS[*]}"

# Runs one split. Usage: run_split RANKS THREADS [extra program options]
run_split() {
    local ranks="$1"
    local threads="$2"
    shift 2
    OMP_NUM_THREADS="$threads" srun --mpi="$MPI_PLUGIN" \
        --nodes="$NODES" --ntasks="$ranks" \
        --ntasks-per-node=$((ranks / NODES)) \
        --cpus-per-task="$threads" --hint=nomultithread --cpu-bind=cores \
        ./build/nbody_hybrid "${ARGS[@]}" "$@"
}

# Check that every split fits the allocation before running anything.
for split in $SPLITS; do
    ranks="${split%x*}"
    threads="${split#*x}"
    if [ $((ranks * threads)) -gt "$CORES" ] || [ $((ranks % NODES)) -ne 0 ]; then
        echo "split $split does not fit $NODES node(s) with $CORES cores" >&2
        exit 1
    fi
done

CHECK_SPLIT=$(echo "$SPLITS" | tr ' ' '\n' | sort -t x -k 1 -n | tail -n 1)
echo "=== check run split=$CHECK_SPLIT"
run_split "${CHECK_SPLIT%x*}" "${CHECK_SPLIT#*x}" --check

for split in $SPLITS; do
    for rep in $(seq 1 "$REPS"); do
        echo "=== timed run split=$split rep=$rep"
        run_split "${split%x*}" "${split#*x}"
    done
done

sacct -j "$SLURM_JOB_ID" --format=JobID,JobName,Elapsed,TotalCPU,AllocCPUS,MaxRSS,NodeList,State 2>/dev/null || true

