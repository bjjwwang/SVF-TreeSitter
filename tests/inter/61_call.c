int *foo(int *p) { return p; }
int main() {
    int a;
    int *q = foo(&a);
    return *q;
}
// EXPECTED:
// pts: a -> {a}
// pts: p -> {a, p}
// pts: q -> {a, q}
// stmts: addr=5 copy=1 load=3 store=2 gep=0
