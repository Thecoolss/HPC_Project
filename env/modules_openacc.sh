# Software environment for the OpenACC backend (build and GPU jobs only).
# Source it (do not execute it) instead of env/modules.sh:
#     . env/modules_openacc.sh && make openacc
#
# nvhpc/25.7 is the NVIDIA HPC SDK (nvc, CUDA 12.9). It is loaded on its own,
# after a purge, because it puts /usr/bin ahead of the EESSI compat layer for
# linking and is not meant to be mixed with the EESSI GCC/OpenMPI modules.
# Build for both GPU generations (Makefile ACC_FLAGS): cc89 on haskell and
# cc120 on fortran/julia. The login node has no GPU, so never rely on
# -gpu=ccnative there. Record any change here and in REPRODUCE.md.
NBODY_ACC_MODULES="${NBODY_ACC_MODULES:-nvhpc/25.7}"

if command -v module >/dev/null 2>&1; then
    module purge
    # shellcheck disable=SC2086 # word splitting is intended (module list)
    module load $NBODY_ACC_MODULES
else
    echo "env/modules_openacc.sh: 'module' command not found" >&2
fi
