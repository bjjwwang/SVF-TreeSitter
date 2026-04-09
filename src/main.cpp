// CLI: ts-svf
#include "svf-ts/TSFrontend.h"
#include "svf-ts/SVFIRVerifier.h"
#include "SVFIR/SVFIR.h"
#include "SVFIR/SVFVariables.h"
#include "Util/SVFUtil.h"
#include "WPA/Andersen.h"
#include <iostream>
#include <string>
#include <vector>

using namespace SVF;

static void usage(const char* p) {
    std::cerr << "Usage: " << p
              << " [--verify] [--dump-svfir] [--ander] [--dump-pts] <source.c>...\n";
}

int main(int argc, char** argv) {
    std::vector<std::string> srcs;
    bool verify = false, dump = false, ander = false, dumpPts = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help") { usage(argv[0]); return 0; }
        else if (a == "--verify")     verify = true;
        else if (a == "--dump-svfir") dump = true;
        else if (a == "--ander")      ander = true;
        else if (a == "--dump-pts")   { dumpPts = true; ander = true; }
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

    if (ander) {
        Andersen* a = AndersenWaveDiff::createAndersenWaveDiff(pag);
        if (dumpPts) {
            // Stable, easy-to-parse format:
            //   PTS <name> -> { <tname1> <tname2> ... }
            // Iterate ALL nodes (top-level ValVars *and* ObjVars), so both
            // p->{p.addr} (top-level) and p.addr->{a.addr} (memory contents)
            // are visible. The extractor normalizes ".addr" away.
            // GepObjVars are created implicitly by Andersen and come back
            // nameless from the SVFIR; synthesise a name from the base
            // object so the L3 normaliser (which keys points-to sets by
            // variable name) can see our field-level pts and match the
            // LLVM frontend's output.
            auto nameOf = [&](SVFVar* v) -> std::string {
                if (!v) return {};
                if (auto* g = SVFUtil::dyn_cast<GepObjVar>(v)) {
                    if (auto* b = g->getBaseObj())
                        return b->getName();
                }
                return v->getName();
            };
            for (auto it = pag->begin(); it != pag->end(); ++it) {
                NodeID id = it->first;
                SVFVar* var = it->second;
                if (!var) continue;
                const PointsTo& pts = a->getPts(id);
                if (pts.empty()) continue;
                std::cout << "PTS " << nameOf(var) << " #" << id << " -> {";
                bool first = true;
                for (NodeID t : pts) {
                    if (!pag->hasGNode(t)) continue;
                    SVFVar* tv = pag->getGNode(t);
                    if (!first) std::cout << " ";
                    std::cout << nameOf(tv) << "#" << t;
                    first = false;
                }
                std::cout << " }\n";
            }
        }
    }

    std::cerr << "SVFIR built: " << pag->getTotalNodeNum() << " nodes\n";
    return 0;
}
