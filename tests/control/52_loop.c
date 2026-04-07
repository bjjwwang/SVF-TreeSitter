int main() {
    int a;
    int *p = &a;
    int i = 0;
    while (i < 10) { *p = i; i = i + 1; }
    return 0;
}
// EXPECTED:
// pts: a -> {a}
// pts: i -> {i}
// pts: p -> {a, p}
// stmts: addr=4 copy=0 load=4 store=4 gep=0
