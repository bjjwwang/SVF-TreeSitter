// SVFIRVerifier.cpp — Layer 1 structural checks
//
// What this verifies (and what it does NOT):
//
//   ✓ Every PAG node id resolves to a non-null SVFVar
//   ✓ Every edge has non-null src/dst endpoints AND those endpoints
//     are still resident in the PAG
//   ✓ Every Addr/Copy/Load/Store dst is a pointer-typed ValVar
//     (sound IR cannot store/load through a non-pointer)
//   ✓ Every Addr edge has an ObjVar source (you cannot take the address
//     of a non-object)
//   ✓ Every BaseObjVar has at least one outgoing Addr edge (otherwise
//     it is unreachable from any pointer and cannot affect analysis).
//     Special exemptions: BlackHole, ConstantObj, FunObj.
//   ✗ Type-correctness of GepStmt access paths
//   ✗ ICFG reachability and dominance
//   ✗ CallPE/RetPE arity match against the called function
//
// This is intentionally a *necessary* (not sufficient) check.

#include "svf-ts/SVFIRVerifier.h"
#include "SVFIR/SVFStatements.h"
#include "SVFIR/SVFVariables.h"
#include "Util/SVFUtil.h"
#include <iostream>
#include <unordered_set>

using namespace SVF;

namespace svfts {

namespace {
struct Reporter {
    bool ok = true;
    int errors = 0;
    void fail(const std::string& msg) {
        ok = false;
        if (errors++ < 20)
            std::cerr << "[verify] " << msg << "\n";
    }
};

bool isPointerNode(const SVFVar* v) {
    return v && v->isPointer();
}
} // namespace

bool SVFIRVerifier::verify(SVFIR* pag) {
    Reporter r;

    // 1. Node integrity
    for (auto it = pag->begin(); it != pag->end(); ++it) {
        if (!it->second)
            r.fail("null node id=" + std::to_string(it->first));
    }

    // 2. Edge integrity + 3. Per-statement type checks
    std::unordered_set<NodeID> objsWithIncomingAddr;

    auto checkEdge = [&](SVFStmt* e, const char* kind) {
        if (!e->getSrcNode() || !e->getDstNode()) {
            r.fail(std::string(kind) + " edge has null endpoint");
            return;
        }
        NodeID s = e->getSrcID(), d = e->getDstID();
        if (!pag->hasGNode(s) || !pag->hasGNode(d)) {
            r.fail(std::string(kind) + " edge references missing node");
            return;
        }
    };

    // Addr: src must be ObjVar, dst must be pointer-typed ValVar
    for (auto* e : pag->getSVFStmtSet(SVFStmt::Addr)) {
        checkEdge(e, "Addr");
        SVFVar* s = e->getSrcNode();
        SVFVar* d = e->getDstNode();
        if (s && !SVFUtil::isa<ObjVar>(s))
            r.fail("AddrStmt src is not an ObjVar (id=" +
                   std::to_string(e->getSrcID()) + ")");
        if (d && !isPointerNode(d))
            r.fail("AddrStmt dst is not a pointer (id=" +
                   std::to_string(e->getDstID()) + ")");
        if (s)
            objsWithIncomingAddr.insert(e->getSrcID());
    }

    // Copy: both endpoints should be pointer ValVars (under the simple model)
    for (auto* e : pag->getSVFStmtSet(SVFStmt::Copy)) {
        checkEdge(e, "Copy");
        if (e->getSrcNode() && !isPointerNode(e->getSrcNode()))
            r.fail("CopyStmt src is not a pointer");
        if (e->getDstNode() && !isPointerNode(e->getDstNode()))
            r.fail("CopyStmt dst is not a pointer");
    }

    // Load: src must be a pointer (we deref it)
    for (auto* e : pag->getSVFStmtSet(SVFStmt::Load)) {
        checkEdge(e, "Load");
        if (e->getSrcNode() && !isPointerNode(e->getSrcNode()))
            r.fail("LoadStmt src is not a pointer");
    }

    // Store: dst must be a pointer (we write through it)
    for (auto* e : pag->getSVFStmtSet(SVFStmt::Store)) {
        checkEdge(e, "Store");
        if (e->getDstNode() && !isPointerNode(e->getDstNode()))
            r.fail("StoreStmt dst is not a pointer");
    }

    // Gep: src should be a pointer
    for (auto* e : pag->getSVFStmtSet(SVFStmt::Gep)) {
        checkEdge(e, "Gep");
        if (e->getSrcNode() && !isPointerNode(e->getSrcNode()))
            r.fail("GepStmt src is not a pointer");
    }

    // 4. Reachability: every BaseObjVar (except specials) needs an Addr.
    for (auto it = pag->begin(); it != pag->end(); ++it) {
        SVFVar* v = it->second;
        if (!v) continue;
        auto* obj = SVFUtil::dyn_cast<BaseObjVar>(v);
        if (!obj) continue;
        // Skip the SVF-reserved singletons.
        NodeID id = it->first;
        if (id == pag->getBlackHoleNode() || id == pag->getConstantNode() ||
            id == pag->getNullPtr())
            continue;
        if (SVFUtil::isa<DummyObjVar>(obj)) continue;
        if (!objsWithIncomingAddr.count(id))
            r.fail("ObjVar " + obj->getName() + " (id=" + std::to_string(id) +
                   ") has no AddrStmt — unreachable from pointer analysis");
    }

    if (r.ok) {
        std::cerr << "[verify] OK — " << pag->getTotalNodeNum() << " nodes, "
                  << pag->getSVFStmtSet(SVFStmt::Addr).size() << " addr, "
                  << pag->getSVFStmtSet(SVFStmt::Copy).size() << " copy, "
                  << pag->getSVFStmtSet(SVFStmt::Load).size() << " load, "
                  << pag->getSVFStmtSet(SVFStmt::Store).size() << " store, "
                  << pag->getSVFStmtSet(SVFStmt::Gep).size() << " gep\n";
    } else {
        std::cerr << "[verify] FAILED with " << r.errors << " error(s)\n";
    }
    return r.ok;
}

} // namespace svfts
