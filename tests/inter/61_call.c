int *foo(int *p) { return p; }
int main() {
    int a;
    int *q = foo(&a);
    return *q;
}
