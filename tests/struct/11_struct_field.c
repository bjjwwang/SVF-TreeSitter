struct Point { int x; int y; };
int main() {
    struct Point s;
    s.x = 1;
    int v = s.x;
    return v;
}
// EXPECTED:
// pts: s -> {s}
// pts: v -> {v}
// stmts: addr=3 copy=0 load=2 store=2 gep=2
