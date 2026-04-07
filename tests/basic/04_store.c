int main() {
    int a;
    int *p = &a;
    *p = 42;
    return 0;
}
// EXPECTED:
// pts: a -> {a}
// pts: p -> {a, p}
// stmts: addr=3 copy=0 load=1 store=2 gep=0
