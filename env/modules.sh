# Shared software environment for building and running on SciTech.
# Source it (do not execute it) before `make` and inside every job script:
#     . env/modules.sh
#
# GCC/12.3.0 was verified on SciTech (job 28010). It is the compiler of the
# OpenMPI/4.1.5-GCC-12.3.0 toolchain named in the course instructions, so
# serial, OpenMP and hybrid builds can share one compiler. Record any change
# here and in REPRODUCE.md.
NBODY_MODULES="${NBODY_MODULES:-GCC/12.3.0}"

if command -v module >/dev/null 2>&1; then
    # shellcheck disable=SC2086 # word splitting is intended (module list)
    module load $NBODY_MODULES
else
    echo "env/modules.sh: 'module' command not found; using the default compiler" >&2
fi
