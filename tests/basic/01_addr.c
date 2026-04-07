int main() {
    int a;
    int *p = &a;
    return 0;
}
// EXPECTED:
// pts: a -> {a}
// pts: p -> {a, p}
// stmts: addr=3 copy=0 load=0 store=1 gep=0
