int main() {
    int a;
    int *arr[4];
    arr[0] = &a;
    int *q = arr[0];
    return 0;
}
// EXPECTED:
// pts: a -> {a}
// pts: arr -> {a, arr}
// pts: q -> {a, q}
// stmts: addr=4 copy=0 load=1 store=2 gep=2
