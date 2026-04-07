struct Node { int data; struct Node *next; };
int main() {
    struct Node n;
    struct Node *p = &n;
    int v = p->data;
    return v;
}
