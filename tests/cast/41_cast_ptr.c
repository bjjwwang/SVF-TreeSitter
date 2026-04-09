struct A { int x; int *pa; };
struct B { int x; int *pb; };
int main() {
    int a;
    struct A sa;
    sa.pa = &a;
    struct B *pb = (struct B*)&sa;
    int *q = pb->pb;
    return 0;
}
// EXPECTED:
// pts: a -> {a}
// pts: sa -> {a, sa}
// pts: pb -> {sa, pb}
// pts: q -> {a, q}
// stmts: addr=5 copy=1 load=2 store=3 gep=2
