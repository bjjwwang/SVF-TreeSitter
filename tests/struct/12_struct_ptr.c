struct Node { int data; struct Node *next; };
int main() {
    struct Node n;
    struct Node *p = &n;
    int v = p->data;
    return v;
}
// EXPECTED:
// pts: n -> {n}
// pts: p -> {n, p}
// pts: v -> {v}
// stmts: addr=4 copy=0 load=3 store=2 gep=1
