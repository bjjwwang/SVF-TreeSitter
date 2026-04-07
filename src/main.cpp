// CLI: ts-svf
#include "svf-ts/TSFrontend.h"
#include "svf-ts/SVFIRVerifier.h"
#include "SVFIR/SVFIR.h"
#include <iostream>
#include <string>
#include <vector>

using namespace SVF;

static void usage(const char* p) {
    std::cerr << "Usage: " << p << " [--verify] [--dump-svfir] <source.c>...\n";
}

int main(int argc, char** argv) {
    std::vector<std::string> srcs;
    bool verify = false, dump = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help") { usage(argv[0]); return 0; }
        else if (a == "--verify")     verify = true;
        else if (a == "--dump-svfir") dump = true;
        else if (a[0] != '-')         srcs.push_back(a);
        else { std::cerr << "Unknown opt " << a << "\n"; return 1; }
    }
    if (srcs.empty()) { usage(argv[0]); return 1; }

    svfts::TSFrontend fe;
    for (auto& s : srcs)
        if (!fe.addSourceFile(s)) { std::cerr << "parse fail: " << s << "\n"; return 1; }
    SVFIR* pag = fe.buildSVFIR();
    if (!pag) { std::cerr << "buildSVFIR failed\n"; return 1; }
    if (verify) {
        svfts::SVFIRVerifier v;
        if (!v.verify(pag)) return 1;
    }
    if (dump) pag->dump("svfir_dump");
    std::cout << "SVFIR built: " << pag->getTotalNodeNum() << " nodes\n";
    return 0;
}
