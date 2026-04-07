int main() {
    int a = 5;
    int *p = &a;
    int x = *p;
    return x;
}
// EXPECTED:
// pts: a -> {a}
// pts: p -> {a, p}
// pts: x -> {x}
// stmts: addr=4 copy=0 load=3 store=3 gep=0
