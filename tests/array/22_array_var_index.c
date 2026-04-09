int main() {
    int a;
    int b;
    int *arr[4];
    arr[0] = &a;
    arr[1] = &b;
    int i;
    int *q = arr[i];
    return 0;
}
// EXPECTED:
// pts: a -> {a}
// pts: b -> {b}
// pts: arr -> {a, b, arr}
// pts: q -> {a, b, q}
// stmts: addr=6 copy=0 load=2 store=3 gep=3
