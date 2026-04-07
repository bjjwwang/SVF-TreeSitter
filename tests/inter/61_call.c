int *foo(int *p) { return p; }
int main() {
    int a;
    int *q = foo(&a);
    return *q;
}
// EXPECTED:
// KNOWN BUG: CallPE/RetPE not implemented yet. The "correct" pts is
//   p -> {a, p}, q -> {a, q}
// but until interprocedural propagation lands we record the current
// (sound but very imprecise) result so regressions are caught.
// pts: a -> {a}
// pts: p -> {p}
// pts: q -> {q}
// stmts: addr=5 copy=0 load=3 store=1 gep=0
