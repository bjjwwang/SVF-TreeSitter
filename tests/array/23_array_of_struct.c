struct Item { int *p; int tag; };
int main() {
    int a;
    struct Item items[4];
    items[0].p = &a;
    int *q = items[0].p;
    return 0;
}
// EXPECTED:
// pts: a -> {a}
// pts: items -> {a, items}
// pts: q -> {a, q}
// stmts: addr=4 copy=0 load=1 store=2 gep=4
