int main() {
    int a;
    int *p = &a;
    int **pp = &p;
    int x = **pp;
    return x;
}
