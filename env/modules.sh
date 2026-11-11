# Shared software environment for building and running on SciTech.
# Source it (do not execute it) before `make` and inside every job script:
#     . env/modules.sh
#
# OpenMPI/4.1.5-GCC-12.3.0 (the toolchain named in the course instructions)
# loads GCC/12.3.0 as a dependency, the compiler of the serial baseline (job
# 28010). Serial, OpenMP and hybrid therefore share one compiler; mpicc wraps
# that same gcc 12.3.0 (checked on rust, issue #1). Record any change here and
# in REPRODUCE.md. The OpenACC backend uses env/modules_openacc.sh instead.
NBODY_MODULES="${NBODY_MODULES:-OpenMPI/4.1.5-GCC-12.3.0}"

if command -v module >/dev/null 2>&1; then
    # shellcheck disable=SC2086 # word splitting is intended (module list)
    module load $NBODY_MODULES
else
    echo "env/modules.sh: 'module' command not found; using the default compiler" >&2
fi
