int main() {
    int a;
    int *p = &a;
    int *q = p + 3;
    return 0;
}
// EXPECTED:
// pts: a -> {a}
// pts: p -> {a, p}
// pts: q -> {a, q}
// stmts: addr=4 copy=0 load=1 store=2 gep=1
