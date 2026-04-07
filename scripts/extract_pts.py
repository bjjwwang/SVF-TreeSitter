#!/usr/bin/env python3
"""
Normalize SVF points-to dumps from either:
  - SVF wpa  -ander -print-all-pts <file.ll>
  - ts-svf   --dump-pts <file.c>

Output format (canonical, sorted):
    <varname> -> {<t1>, <t2>, ...}

Usage:
    python3 extract_pts.py wpa  < wpa_output.txt
    python3 extract_pts.py ts   < tssvf_output.txt
"""
import sys
import re

# ----- common cleanup -----
SKIP_PREFIXES = ("tmp", ".", "%")  # compiler temporaries

def normalize(name: str) -> str:
    """Strip our per-variable storage suffix so x.addr matches LLVM's x."""
    if name.endswith(".addr"):
        return name[:-len(".addr")]
    return name

def looks_like_temp(name: str) -> bool:
    if not name or name.isdigit():
        return True
    if name.startswith(SKIP_PREFIXES):
        return True
    return False

# ----- parse ts-svf format -----
TS_LINE = re.compile(
    r'^PTS\s+(\S*)\s+#(\d+)\s+->\s+\{([^}]*)\}\s*$'
)
TS_TARGET = re.compile(r'(\S+?)#(\d+)')

def parse_ts(lines):
    pts = {}
    for line in lines:
        m = TS_LINE.match(line.rstrip())
        if not m:
            continue
        name = normalize(m.group(1))
        if looks_like_temp(name):
            continue
        targets = set()
        for tm in TS_TARGET.finditer(m.group(3)):
            tname = normalize(tm.group(1))
            if looks_like_temp(tname):
                continue
            targets.add(tname)
        if targets:
            pts.setdefault(name, set()).update(targets)
    return pts

# ----- parse wpa format -----
# Matches blocks like:
#   ##<a> Source Loc: ...
#   Ptr 11 		PointsTo: { 13 }
#   !!Target NodeID 13	 [<p> Source Loc: ]
WPA_HDR    = re.compile(r'^##<([^>]*)>')
WPA_PTR    = re.compile(r'^Ptr\s+(\d+)\s+PointsTo:\s*\{([^}]*)\}')
WPA_TARGET = re.compile(r'^!!Target NodeID\s+(\d+)\s+\[<([^>]*)>')

def parse_wpa(lines):
    pts = {}
    cur_name = None
    cur_targets = None  # set of target ids until we resolve names
    id2name = {}
    pending = []  # list of (cur_name, set_of_ids)
    for line in lines:
        h = WPA_HDR.match(line)
        if h:
            cur_name = h.group(1).strip()
            cur_targets = None
            continue
        p = WPA_PTR.match(line)
        if p and cur_name is not None:
            ids = set()
            for tok in p.group(2).split():
                if tok.isdigit():
                    ids.add(int(tok))
            cur_targets = ids
            pending.append((cur_name, ids))
            continue
        t = WPA_TARGET.match(line)
        if t:
            id2name[int(t.group(1))] = t.group(2).strip()
            continue
    for name, ids in pending:
        name = normalize(name)
        if looks_like_temp(name):
            continue
        targets = set()
        for tid in ids:
            tname = id2name.get(tid)
            if tname is None:
                continue
            tname = normalize(tname)
            if looks_like_temp(tname):
                continue
            targets.add(tname)
        if targets:
            pts.setdefault(name, set()).update(targets)
    return pts

def emit(pts):
    for name in sorted(pts):
        ts = ", ".join(sorted(pts[name]))
        print(f"{name} -> {{{ts}}}")

def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ("ts", "wpa"):
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    lines = sys.stdin.readlines()
    pts = parse_ts(lines) if sys.argv[1] == "ts" else parse_wpa(lines)
    emit(pts)

if __name__ == "__main__":
    main()
