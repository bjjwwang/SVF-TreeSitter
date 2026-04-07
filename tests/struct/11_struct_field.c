struct Point { int x; int y; };
int main() {
    struct Point s;
    s.x = 1;
    int v = s.x;
    return v;
}
// EXPECTED:
// KNOWN GAP: GepHandler emits variant geps only — no field-name → idx
// resolution yet, so all struct accesses collapse to the base obj.
// pts: s -> {s}
// pts: v -> {v}
// stmts: addr=3 copy=0 load=2 store=2 gep=2
