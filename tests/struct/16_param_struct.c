struct S { int *p; };
void foo(struct S *s, int *v) { s->p = v; }
int main() {
    int a;
    struct S s;
    foo(&s, &a);
    int *q = s.p;
    return 0;
}
// EXPECTED:
// pts: a -> {a}
// pts: s -> {a, s}
// pts: q -> {a, q}
// pts: v -> {a, v}
// stmts: addr=7 copy=0 load=3 store=4 gep=2
