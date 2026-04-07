#include "svf-ts/SVFIRVerifier.h"
#include "SVFIR/SVFStatements.h"
#include <iostream>

using namespace SVF;

namespace svfts {

bool SVFIRVerifier::verify(SVFIR* pag) {
    bool ok = true;
    // Walk all nodes; ensure each is non-null.
    for (auto it = pag->begin(); it != pag->end(); ++it) {
        if (it->second == nullptr) {
            std::cerr << "[verify] null node id=" << it->first << "\n";
            ok = false;
        }
    }
    // Walk all stmt edges from each node and ensure src/dst exist
    for (auto it = pag->begin(); it != pag->end(); ++it) {
        SVFVar* v = it->second;
        if (!v) continue;
        for (auto* e : v->getOutEdges()) {
            if (!e->getSrcNode() || !e->getDstNode()) {
                std::cerr << "[verify] edge has null endpoint\n";
                ok = false;
            }
        }
    }
    if (ok) std::cerr << "[verify] OK — " << pag->getTotalNodeNum() << " nodes\n";
    return ok;
}

} // namespace svfts
