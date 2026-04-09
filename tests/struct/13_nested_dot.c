struct Inner { int x; int *px; };
struct Outer { int head; struct Inner inner; };
int main() {
    int a;
    struct Outer s;
    s.inner.px = &a;
    int *q = s.inner.px;
    return 0;
}
// EXPECTED:
// pts: a -> {a}
// pts: s -> {a, s}
// pts: q -> {a, q}
// stmts: addr=4 copy=0 load=1 store=2 gep=4
