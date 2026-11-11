CC ?= cc
OPTFLAGS ?= -O2
WARNFLAGS = -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes
# Use `make WERROR=` to keep warnings visible without failing the build
# (e.g. when trying a new cluster compiler for the first time).
WERROR ?= -Werror
# Plain assignment so an environment CFLAGS cannot silently drop the warnings.
CFLAGS = -std=c11 $(OPTFLAGS) $(WARNFLAGS) $(WERROR)
CPPFLAGS = -Iinclude
LDLIBS = -lm
# OpenMP flags. GCC on the cluster takes -fopenmp. On macOS (Apple clang +
# Homebrew libomp) override them, e.g.:
#   make OMP_CFLAGS='-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include' \
#        OMP_LDLIBS='-L/opt/homebrew/opt/libomp/lib -lomp'
OMP_CFLAGS ?= -fopenmp
OMP_LDLIBS ?= -fopenmp
# MPI compiler wrapper for the hybrid backend (OpenMPI module, env/modules.sh).
MPICC ?= mpicc
# OpenACC backend (nvhpc module, env/modules_openacc.sh). One binary for both
# GPU generations: cc89 (haskell, RTX 6000 Ada) and cc120 (fortran/julia,
# RTX PRO 6000 Blackwell). No -Werror: nvc's own math.h makes isfinite() raise
# a pedantic warning (issue #1); the warnings stay visible.
ACC_CC ?= nvc
ACC_FLAGS ?= -acc -gpu=cc89,cc120 -Minfo=accel
ACC_CFLAGS = -std=c11 $(OPTFLAGS) $(WARNFLAGS) $(ACC_FLAGS)

GIT_COMMIT := $(shell git describe --always --dirty --abbrev=12 2>/dev/null || echo unavailable)
BUILD_INFO = $(GIT_COMMIT) | $(CC) | $(CFLAGS) | $(OMP_CFLAGS) | $(MPICC) | $(ACC_CC) | $(ACC_CFLAGS)
# Build metadata embedded in a binary: $(call build_meta,COMPILER,FLAGS).
# FLAGS must be the flags that binary was really compiled with.
build_meta = -DNBODY_COMPILER='"$(1)"' -DNBODY_BUILD_FLAGS='"$(2)"' \
             -DNBODY_GIT_COMMIT='"$(GIT_COMMIT)"'
BUILD_INFO_CPPFLAGS = $(call build_meta,$(CC),$(CFLAGS))

BUILD_DIR := build
HEADERS := include/nbody.h include/config.h include/report.h
COMMON_SOURCES := src/particles.c src/integrator.c src/diagnostics.c \
                  src/config.c src/report.c
SERIAL_SOURCES := $(COMMON_SOURCES) src/force_serial.c

.PHONY: all test test-hybrid hybrid openacc clean FORCE

# `all` needs the OpenMPI module (env/modules.sh) because of nbody_hybrid.
all: $(BUILD_DIR)/nbody_serial $(BUILD_DIR)/nbody_openmp $(BUILD_DIR)/nbody_hybrid

$(BUILD_DIR):
	mkdir -p $@

# Rewritten only when the commit, compiler or flags change, so binaries are
# rebuilt whenever their embedded build metadata would be stale.
$(BUILD_DIR)/build_info: FORCE | $(BUILD_DIR)
	@echo '$(BUILD_INFO)' | cmp -s - $@ || echo '$(BUILD_INFO)' > $@

$(BUILD_DIR)/nbody_serial: $(SERIAL_SOURCES) src/main_serial.c $(HEADERS) $(BUILD_DIR)/build_info
	$(CC) $(CPPFLAGS) $(BUILD_INFO_CPPFLAGS) $(CFLAGS) $(SERIAL_SOURCES) src/main_serial.c -o $@ $(LDLIBS)

$(BUILD_DIR)/nbody_openmp: $(COMMON_SOURCES) src/force_openmp.c src/main_openmp.c $(HEADERS) $(BUILD_DIR)/build_info
	$(CC) $(CPPFLAGS) $(call build_meta,$(CC),$(CFLAGS) $(OMP_CFLAGS)) $(CFLAGS) $(OMP_CFLAGS) $(COMMON_SOURCES) src/force_openmp.c src/main_openmp.c -o $@ $(LDLIBS) $(OMP_LDLIBS)

# Hybrid MPI+OpenMP backend; `make hybrid` builds only this one.
HYBRID_SOURCES := $(COMMON_SOURCES) src/force_openmp.c src/decomposition.c

hybrid: $(BUILD_DIR)/nbody_hybrid

$(BUILD_DIR)/nbody_hybrid: $(HYBRID_SOURCES) src/main_hybrid.c $(HEADERS) $(BUILD_DIR)/build_info
	$(MPICC) $(CPPFLAGS) $(call build_meta,$(MPICC),$(CFLAGS) $(OMP_CFLAGS)) $(CFLAGS) $(OMP_CFLAGS) $(HYBRID_SOURCES) src/main_hybrid.c -o $@ $(LDLIBS) $(OMP_LDLIBS)

# OpenACC backend. Not part of `all`: it needs the nvhpc module instead of
# env/modules.sh, and a GPU node to run. Build it with
#     . env/modules_openacc.sh && make openacc
ACC_SOURCES := $(COMMON_SOURCES) src/force_openacc.c

openacc: $(BUILD_DIR)/nbody_openacc

$(BUILD_DIR)/nbody_openacc: $(ACC_SOURCES) src/main_openacc.c $(HEADERS) $(BUILD_DIR)/build_info
	$(ACC_CC) $(CPPFLAGS) $(call build_meta,$(ACC_CC),$(ACC_CFLAGS)) $(ACC_CFLAGS) $(ACC_SOURCES) src/main_openacc.c -o $@ $(LDLIBS)

# partition_range unit tests: plain C, no MPI, runs anywhere.
$(BUILD_DIR)/test_hybrid: src/decomposition.c tests/test_hybrid.c include/nbody.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/decomposition.c tests/test_hybrid.c -o $@ $(LDLIBS)

test-hybrid: $(BUILD_DIR)/test_hybrid
	./$(BUILD_DIR)/test_hybrid

$(BUILD_DIR)/test_nbody: $(SERIAL_SOURCES) tests/test_nbody.c $(HEADERS) $(BUILD_DIR)/build_info
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SERIAL_SOURCES) tests/test_nbody.c -o $@ $(LDLIBS)

openacc: $(BUILD_DIR)/nbody_openacc

$(BUILD_DIR)/build_info_openacc: FORCE | $(BUILD_DIR)
	@echo '$(ACC_BUILD_INFO)' | cmp -s - $@ || echo '$(ACC_BUILD_INFO)' > $@

$(BUILD_DIR)/nbody_openacc: $(ACC_SOURCES) src/main_openacc.c $(HEADERS) $(BUILD_DIR)/build_info_openacc
	$(ACC_CC) $(CPPFLAGS) -DNBODY_COMPILER='"$(ACC_CC)"' \
	    -DNBODY_BUILD_FLAGS='"$(ACC_CFLAGS)"' -DNBODY_GIT_COMMIT='"$(GIT_COMMIT)"' \
	    $(ACC_CFLAGS) $(ACC_SOURCES) src/main_openacc.c -o $@ $(LDLIBS)

$(BUILD_DIR)/test_openacc: $(ACC_SOURCES) src/force_serial.c tests/test_openacc.c $(HEADERS)
	$(ACC_CC) $(CPPFLAGS) $(ACC_CFLAGS) $(ACC_SOURCES) src/force_serial.c tests/test_openacc.c -o $@ $(LDLIBS)

test_openacc: $(BUILD_DIR)/test_openacc
	./$(BUILD_DIR)/test_openacc

# OpenMP kernel tests: thread counts 1-8 against serial, sub-ranges, inputs.
$(BUILD_DIR)/test_openmp: $(SERIAL_SOURCES) src/force_openmp.c tests/test_openmp.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OMP_CFLAGS) $(SERIAL_SOURCES) src/force_openmp.c tests/test_openmp.c -o $@ $(LDLIBS) $(OMP_LDLIBS)

# Everything here runs on one core without MPI or a GPU, so `make test` is
# safe on the login node. MPI end-to-end checks: tests/test_hybrid.sh (Slurm).
test: $(BUILD_DIR)/nbody_serial $(BUILD_DIR)/nbody_openmp $(BUILD_DIR)/test_nbody \
      $(BUILD_DIR)/test_openmp $(BUILD_DIR)/test_hybrid
	./$(BUILD_DIR)/test_nbody
	./$(BUILD_DIR)/test_openmp
	./$(BUILD_DIR)/test_hybrid
	sh tests/test_cli.sh ./$(BUILD_DIR)/nbody_serial
	OMP_NUM_THREADS=2 sh tests/test_cli.sh ./$(BUILD_DIR)/nbody_openmp

clean:
	rm -rf $(BUILD_DIR)
