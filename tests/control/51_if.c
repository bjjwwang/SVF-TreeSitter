int main() {
    int a, b;
    int *p;
    if (a) p = &a; else p = &b;
    return *p;
}
