#!/bin/sh
set -eu

program=./build/nbody_serial
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

for args in \
    "--n 0" \
    "--steps 0" \
    "--dt 0" \
    "--dt nan" \
    "--eps -1" \
    "--seed -1" \
    "--unknown 1" \
    "--n" \
    "--output"
do
    if "$program" $args >/dev/null 2>&1; then
        printf 'Expected invalid arguments to fail: %s\n' "$args" >&2
        exit 1
    fi
done

output=$("$program" --n 1 --steps 1 --check)
for field in backend=serial mpi_ranks=1 omp_threads=1 nodes=1 \
    git_commit= slurm_job_id= host= build_flags= seconds_per_step= \
    interactions_per_second= initial_energy= relative_energy_change=
do
    if ! printf '%s\n' "$output" | grep -q "$field"; then
        printf 'Missing output field: %s\n' "$field" >&2
        exit 1
    fi
done

# --output writes a header plus one line of six values per particle.
"$program" --n 7 --steps 2 --output "$tmpdir/state.txt" >/dev/null
test "$(head -n 1 "$tmpdir/state.txt" | cut -c1-14)" = '# nbody_state '
test "$(grep -vc '^#' "$tmpdir/state.txt")" -eq 7
test "$(awk '!/^#/ { print NF }' "$tmpdir/state.txt" | sort -u)" = 6

# Two identical serial runs must compare equal.
"$program" --n 7 --steps 2 --output "$tmpdir/state2.txt" >/dev/null
cmp -s "$tmpdir/state.txt" "$tmpdir/state2.txt"

# An unwritable output path must produce a nonzero exit status.
if "$program" --n 2 --steps 1 --output "$tmpdir/missing/dir/state.txt" \
    >/dev/null 2>&1; then
    printf 'Expected unwritable --output path to fail\n' >&2
    exit 1
fi

printf '%s\n' 'All CLI tests passed'
