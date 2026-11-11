#!/bin/sh
# End-to-end check of the hybrid backend against the serial reference.
# Runs nbody_hybrid with several rank counts (including ones that do not
# divide N) and compares each final state with scripts/compare_states.py.
#
# Needs MPI, so run it inside a short Slurm job or an interactive allocation
# on the cluster, never on the login node:
#     salloc -p cpu -n 4 -t 00:05:00
#     . env/modules.sh && make build/nbody_serial build/nbody_hybrid
#     sh tests/test_hybrid.sh
# MPIRUN defaults to srun inside Slurm and mpirun elsewhere; override with
#     MPIRUN="mpirun -n" sh tests/test_hybrid.sh
set -eu

N="${N:-1000}"
STEPS="${STEPS:-100}"
RANK_COUNTS="${RANK_COUNTS:-1 2 3 4}"
if [ -n "${SLURM_JOB_ID:-}" ]; then
    MPIRUN="${MPIRUN:-srun --mpi=pmix -n}"
else
    MPIRUN="${MPIRUN:-mpirun -n}"
fi

# Ranks may run on other nodes than this script, so in multi-node jobs set
# TMPDIR to a shared directory (slurm/validate_hybrid.sbatch does this).
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/nbody_test_hybrid.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT

./build/nbody_serial --n "$N" --steps "$STEPS" --output "$tmpdir/ref.txt" \
    >/dev/null

for ranks in $RANK_COUNTS; do
    OMP_NUM_THREADS="${OMP_NUM_THREADS:-1}" $MPIRUN "$ranks" \
        ./build/nbody_hybrid --n "$N" --steps "$STEPS" \
        --output "$tmpdir/hybrid_$ranks.txt" >/dev/null
    if python3 scripts/compare_states.py "$tmpdir/ref.txt" \
        "$tmpdir/hybrid_$ranks.txt"; then
        echo "hybrid ranks=$ranks: PASS"
    else
        echo "hybrid ranks=$ranks: FAIL (does not match serial)" >&2
        exit 1
    fi
done
