struct Box { int *p; int tag; };
int a;
struct Box g;
int main() {
    g.p = &a;
    int *q = g.p;
    return 0;
}
// EXPECTED:
// pts: a -> {a}
// pts: g -> {a, g}
// pts: q -> {a, q}
// stmts: addr=4 copy=0 load=1 store=2 gep=2
