#!/usr/bin/env python3
"""Compare two nbody --output state files within tolerances.

Usage: compare_states.py REFERENCE CANDIDATE [--atol A] [--rtol R]

Each value v in CANDIDATE must satisfy |v - r| <= atol + rtol * |r| against
the corresponding reference value r. Exits 0 on success, 1 on mismatch and
2 on malformed input. Standard library only.
"""

import argparse
import sys

COLUMNS = ("x", "y", "z", "vx", "vy", "vz")


def read_state(path):
    header = None
    rows = []
    with open(path, encoding="ascii") as handle:
        for line_number, line in enumerate(handle, start=1):
            if line.startswith("#"):
                header = line.strip()
                continue
            fields = line.split()
            if len(fields) != len(COLUMNS):
                raise ValueError(f"{path}:{line_number}: expected 6 values")
            rows.append([float(value) for value in fields])
    if header is None:
        raise ValueError(f"{path}: missing '# nbody_state' header")
    return header, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("reference")
    parser.add_argument("candidate")
    parser.add_argument("--atol", type=float, default=1e-10)
    parser.add_argument("--rtol", type=float, default=1e-8)
    args = parser.parse_args()

    try:
        ref_header, ref = read_state(args.reference)
        cand_header, cand = read_state(args.candidate)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    if len(ref) != len(cand):
        print(f"error: particle counts differ ({len(ref)} vs {len(cand)})",
              file=sys.stderr)
        return 2

    max_abs = [0.0] * len(COLUMNS)
    failures = 0
    for i, (ref_row, cand_row) in enumerate(zip(ref, cand)):
        for k, (r, c) in enumerate(zip(ref_row, cand_row)):
            diff = abs(c - r)
            max_abs[k] = max(max_abs[k], diff)
            if diff > args.atol + args.rtol * abs(r):
                if failures < 5:
                    print(f"mismatch particle={i} {COLUMNS[k]} "
                          f"ref={r!r} cand={c!r} diff={diff:.3e}")
                failures += 1

    print(f"reference: {ref_header}")
    print(f"candidate: {cand_header}")
    print("max_abs_diff " + " ".join(
        f"{name}={value:.3e}" for name, value in zip(COLUMNS, max_abs)))
    print(f"atol={args.atol:g} rtol={args.rtol:g} n={len(ref)} "
          f"failures={failures}")
    print("PASS" if failures == 0 else "FAIL")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
