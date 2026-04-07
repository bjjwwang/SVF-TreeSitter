int main() {
    int a;
    int *p = &a;
    int i = 0;
    while (i < 10) { *p = i; i = i + 1; }
    return 0;
}
