int main() {
    int a, b;
    int *p;
    if (a) p = &a; else p = &b;
    return *p;
}
// EXPECTED:
// Flow-insensitive: p picks up both branches.
// pts: a -> {a}
// pts: b -> {b}
// pts: p -> {a, b, p}
// stmts: addr=4 copy=0 load=3 store=2 gep=0
