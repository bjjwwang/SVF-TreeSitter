#!/usr/bin/env python3
"""
Compare two normalized points-to set files.

Reports:
  exact:    actual == expected
  superset: actual ⊋ expected (sound but imprecise — OK)
  unsound:  actual ⊊ expected (BUG)
  mismatch: incomparable difference

Exit code 0 iff no unsound entries.

Usage:
    python3 compare_pts.py expected.txt actual.txt
"""
import sys

def parse(path):
    pts = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if "->" not in line:
                continue
            var, rest = line.split("->", 1)
            var = var.strip()
            inner = rest.strip().lstrip("{").rstrip("}").strip()
            targets = set(t.strip() for t in inner.split(",") if t.strip())
            if targets:
                pts[var] = targets
    return pts

def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    expected = parse(sys.argv[1])
    actual   = parse(sys.argv[2])

    exact = unsound = imprecise = mismatch = 0
    overlap = sorted(set(expected) & set(actual))

    for v in overlap:
        e, a = expected[v], actual[v]
        if e == a:
            exact += 1
        elif e < a:
            imprecise += 1
            print(f"  IMPRECISE {v}: expected {sorted(e)} got {sorted(a)}")
        elif a < e:
            unsound += 1
            print(f"  *** UNSOUND *** {v}: expected {sorted(e)} got {sorted(a)}")
            print(f"      missing: {sorted(e - a)}")
        else:
            mismatch += 1
            print(f"  MISMATCH {v}: expected {sorted(e)} got {sorted(a)}")

    only_expected = sorted(set(expected) - set(actual))
    only_actual   = sorted(set(actual) - set(expected))
    if only_expected:
        print(f"  vars only in expected (potentially unsound): {only_expected}")
    if only_actual:
        print(f"  vars only in actual (extra): {only_actual}")

    print(f"\nResults: {exact} exact, {imprecise} imprecise (OK), "
          f"{unsound} UNSOUND, {mismatch} mismatch, "
          f"{len(only_expected)} missing-vars, {len(only_actual)} extra-vars")
    sys.exit(0 if unsound == 0 and mismatch == 0 else 1)

if __name__ == "__main__":
    main()
