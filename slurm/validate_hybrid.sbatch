#!/bin/bash -l
#SBATCH --job-name=nbody-validate-hybrid
#SBATCH --output=results/logs/validate_hybrid_%j.out
#SBATCH --partition=cpu
#SBATCH --time=00:10:00
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=8
#SBATCH --cpus-per-task=1
#SBATCH --hint=nomultithread
#SBATCH --mem=4G
#
# Correctness of the hybrid backend on the cluster: runs tests/test_hybrid.sh,
# which compares the final state of nbody_hybrid with nbody_serial
# (scripts/compare_states.py, tolerance from docs/INTERFACE.md §10).
#
#   pass 1: one node, pure MPI, rank counts that do and do not divide N
#   pass 2: two nodes, pure MPI (exchange goes over the network)
#   pass 3: two nodes, 2 OpenMP threads per rank (hybrid)
#
# Small workload (N=1000, 100 steps), about a minute. Submit from the
# repository root after building:
#     . env/modules.sh && make && sbatch slurm/validate_hybrid.sbatch
# The job fails (State=FAILED) if any comparison fails.

cd "${SLURM_SUBMIT_DIR:?run this script with sbatch}"

# Load modules before `set -u`; module scripts may read unset variables.
. env/modules.sh
set -euo pipefail

export N="${N:-1000}"
export STEPS="${STEPS:-100}"
MPI_PLUGIN="${MPI_PLUGIN:-pmix}"

# tests/test_hybrid.sh writes state files from every node, so its temporary
# directory must be on the shared filesystem, not node-local /tmp or
# /var/tmp (job 28687: rank 0 on haskell could not write to a /var/tmp
# directory created on fortran). build/ is git-ignored.
export TMPDIR="$SLURM_SUBMIT_DIR/build/tmp"
mkdir -p "$TMPDIR"

export OMP_PLACES=cores
export OMP_PROC_BIND=close
# Same inter-node network restriction as slurm/hybrid.sbatch (job 28606).
export OMPI_MCA_btl_tcp_if_include="${OMPI_MCA_btl_tcp_if_include:-10.205.20.0/24}"
# Open MPI transport: use Open MPI's own ob1 layer with shared memory (vader)
# inside a node and TCP between nodes, not UCX. With UCX, 16 ranks on 2 nodes
# segfaulted in libucs on haskell 3/3 times (jobs 28689, 28690); with these
# settings the same run passed 3/3 (job 28690). Keep them for every hybrid run
# so all timings use the same transport.
export OMPI_MCA_pml="${OMPI_MCA_pml:-ob1}"
export OMPI_MCA_btl="${OMPI_MCA_btl:-self,vader,tcp}"

for exe in build/nbody_serial build/nbody_hybrid; do
    if [ ! -x "$exe" ]; then
        echo "$exe not found; run 'make' first" >&2
        exit 1
    fi
done

sh scripts/job_metadata.sh
echo "mpi_version=$(mpirun --version 2>/dev/null | head -n 1)"
echo "mpi_tcp_networks=$OMPI_MCA_btl_tcp_if_include"
echo "mpi_pml=$OMPI_MCA_pml mpi_btl=$OMPI_MCA_btl"
echo "workload: n=$N steps=$STEPS"

SRUN="srun --mpi=$MPI_PLUGIN --hint=nomultithread --cpu-bind=cores"

echo "=== pass 1: one node, 1 thread per rank"
OMP_NUM_THREADS=1 RANK_COUNTS="1 2 3 4 7 8" \
    MPIRUN="$SRUN --nodes=1 -c 1 -n" sh tests/test_hybrid.sh

echo "=== pass 2: two nodes, 1 thread per rank"
OMP_NUM_THREADS=1 RANK_COUNTS="2 3 8 16" \
    MPIRUN="$SRUN --nodes=2 -c 1 -n" sh tests/test_hybrid.sh

echo "=== pass 3: two nodes, 2 threads per rank"
OMP_NUM_THREADS=2 RANK_COUNTS="2 4 8" \
    MPIRUN="$SRUN --nodes=2 -c 2 -n" sh tests/test_hybrid.sh

echo "=== all hybrid validation passes: PASS"

sacct -j "$SLURM_JOB_ID" --format=JobID,JobName,Elapsed,AllocCPUS,NodeList,State 2>/dev/null || true
