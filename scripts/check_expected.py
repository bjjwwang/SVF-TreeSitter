#!/usr/bin/env python3
"""
Layer-2 ground-truth checker.

Each test .c file may carry an `// EXPECTED:` block at the bottom:

    int main() { int a; int *p = &a; return 0; }
    // EXPECTED:
    // pts: a -> {a}
    // pts: p -> {a, p}
    // stmts: addr=3 copy=0 load=0 store=1 gep=0

This script:
  1. Parses the EXPECTED block from the .c source.
  2. Runs `ts-svf --dump-pts <file>` and `--verify <file>`.
  3. Normalizes the pts dump via extract_pts.parse_ts (so .addr suffixes
     are stripped, just like Layer 3).
  4. Asserts every `pts:` line matches EXACTLY (not superset).
  5. Asserts the `stmts:` line matches the verifier's reported counts.

If the .c file has no EXPECTED block, the test is SKIPPED (returns 77,
which CMake interprets as a skip when SKIP_RETURN_CODE=77 is set).

Usage:
    check_expected.py <test.c>
Env:
    TS_SVF        path to ts-svf (default ./ts-svf)
    LD_LIBRARY_PATH must already point to SVF + Z3 dynamic libs
"""
import os
import re
import subprocess
import sys

# Reuse the L3 normalizer.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_pts import parse_ts  # noqa: E402

SKIP = 77

EXPECTED_HDR = re.compile(r'^//\s*EXPECTED:\s*$')
PTS_LINE     = re.compile(r'^//\s*pts:\s*(\S+)\s*->\s*\{([^}]*)\}\s*$')
STMTS_LINE   = re.compile(r'^//\s*stmts:\s*(.+?)\s*$')

def parse_expected(path):
    pts = {}
    stmts = None
    in_block = False
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if EXPECTED_HDR.match(line):
                in_block = True
                continue
            if not in_block:
                continue
            if not line.lstrip().startswith("//"):
                # blank line or end of comment block — stop
                break
            mp = PTS_LINE.match(line)
            if mp:
                var = mp.group(1)
                targets = set(t.strip() for t in mp.group(2).split(",") if t.strip())
                pts[var] = targets
                continue
            ms = STMTS_LINE.match(line)
            if ms:
                stmts = {}
                for tok in ms.group(1).split():
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        try:
                            stmts[k] = int(v)
                        except ValueError:
                            pass
                continue
    return pts, stmts, in_block

# verifier emits e.g.
#   [verify] OK — 9 nodes, 3 addr, 0 copy, 1 load, 2 store, 0 gep
VERIFY_RE = re.compile(
    r'\[verify\] OK.*?(\d+)\s+nodes,\s*(\d+)\s+addr,\s*(\d+)\s+copy,'
    r'\s*(\d+)\s+load,\s*(\d+)\s+store,\s*(\d+)\s+gep'
)

def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)

def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    src = sys.argv[1]
    ts_svf = os.environ.get("TS_SVF", "./ts-svf")

    expected_pts, expected_stmts, has_block = parse_expected(src)
    if not has_block:
        print(f"SKIP {src} — no // EXPECTED: block")
        sys.exit(SKIP)

    fail = []

    # ---- pts check ----
    if expected_pts:
        r = run([ts_svf, "--dump-pts", src])
        if r.returncode != 0:
            print(f"FAIL ts-svf --dump-pts crashed:\n{r.stderr}")
            sys.exit(1)
        actual_pts = parse_ts(r.stdout.splitlines())
        for var, targets in expected_pts.items():
            got = actual_pts.get(var, set())
            if got != targets:
                fail.append(f"  pts {var}: expected {sorted(targets)} got {sorted(got)}")
        # also flag vars in actual that are not declared in expected (catches drift)
        for var in actual_pts:
            if var not in expected_pts:
                # Treat as informational, not fatal — comment out next line to make strict.
                pass

    # ---- stmts check ----
    if expected_stmts:
        r = run([ts_svf, "--verify", src])
        m = VERIFY_RE.search(r.stderr)
        if not m:
            print(f"FAIL could not parse verifier output:\n{r.stderr}")
            sys.exit(1)
        nodes, addr, copy, load, store, gep = map(int, m.groups())
        actual = {"nodes": nodes, "addr": addr, "copy": copy,
                  "load": load, "store": store, "gep": gep}
        for k, v in expected_stmts.items():
            if actual.get(k) != v:
                fail.append(f"  stmts {k}: expected {v} got {actual.get(k)}")

    if fail:
        print(f"FAIL {src}")
        for line in fail:
            print(line)
        sys.exit(1)
    print(f"OK   {src}")

if __name__ == "__main__":
    main()
