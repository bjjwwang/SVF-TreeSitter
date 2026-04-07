int g;
int *gp = &g;
int main() {
    return *gp;
}
// EXPECTED:
// pts: g -> {g}
// pts: gp -> {g, gp}
// stmts: addr=3 copy=0 load=2 store=1 gep=0
