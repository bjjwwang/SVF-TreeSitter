int main() {
    int a;
    int *p = &a;
    int **pp = &p;
    int x = **pp;
    return x;
}
// EXPECTED:
// pts: a -> {a}
// pts: p -> {a, p}
// pts: pp -> {p, pp}
// pts: x -> {x}
// stmts: addr=5 copy=0 load=4 store=3 gep=0
