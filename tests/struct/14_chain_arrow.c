struct Node { int data; struct Node *next; };
int main() {
    int a;
    struct Node n1;
    struct Node n2;
    n2.next = &n1;
    n1.next = 0;
    struct Node *p = &n2;
    struct Node *q = p->next;
    return 0;
}
// EXPECTED:
// pts: a -> {a}
// pts: n1 -> {n1}
// pts: n2 -> {n1, n2}
// pts: p -> {n2, p}
// pts: q -> {n1, q}
// stmts: addr=6 copy=0 load=2 store=4 gep=3
