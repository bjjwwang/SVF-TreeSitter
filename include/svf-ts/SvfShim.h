// SvfShim.h — befriended classes that expose SVF's private builder API.
//
// SVFIR declares `friend class SVFIRBuilder;` and ICFG declares
// `friend class ICFGBuilder;`. The real implementations of those classes
// live in `svf-llvm`, which we deliberately do NOT link. Friendship is by
// name though, so we redeclare these classes in `namespace SVF` and use
// them as thin pass-throughs to call the otherwise-private add* methods.
//
// Linking is safe: libSvfCore.{a,so} contains no symbols for SVFIRBuilder
// or ICFGBuilder (they live in libSvfLLVM, which we never link).

#pragma once

#include "SVFIR/SVFIR.h"
#include "Graphs/ICFG.h"
#include "Graphs/ICFGNode.h"
#include "SVFIR/SVFType.h"

namespace SVF {

class SVFIRBuilder {
public:
    explicit SVFIRBuilder(SVFIR* p) : pag(p) {}

    NodeID addValNode(NodeID i, const SVFType* type, const ICFGNode* icn) {
        return pag->addValNode(i, type, icn);
    }
    NodeID addObjNode(NodeID i, ObjTypeInfo* ti, const ICFGNode* icn) {
        return pag->addObjNode(i, ti, icn);
    }
    NodeID addStackObjNode(NodeID i, ObjTypeInfo* ti, const ICFGNode* icn) {
        return pag->addStackObjNode(i, ti, icn);
    }
    NodeID addHeapObjNode(NodeID i, ObjTypeInfo* ti, const ICFGNode* icn) {
        return pag->addHeapObjNode(i, ti, icn);
    }
    NodeID addGlobalObjNode(NodeID i, ObjTypeInfo* ti, const ICFGNode* icn) {
        return pag->addGlobalObjNode(i, ti, icn);
    }
    NodeID addFunObjNode(NodeID i, ObjTypeInfo* ti, const ICFGNode* icn) {
        return pag->addFunObjNode(i, ti, icn);
    }
    NodeID addConstantAggObjNode(NodeID i, ObjTypeInfo* ti, const ICFGNode* icn) {
        return pag->addConstantAggObjNode(i, ti, icn);
    }
    AddrStmt* addAddrStmt(NodeID s, NodeID d) { return pag->addAddrStmt(s, d); }
    CopyStmt* addCopyStmt(NodeID s, NodeID d, CopyStmt::CopyKind k = CopyStmt::COPYVAL) {
        return pag->addCopyStmt(s, d, k);
    }
    LoadStmt* addLoadStmt(NodeID s, NodeID d) { return pag->addLoadStmt(s, d); }
    StoreStmt* addStoreStmt(NodeID s, NodeID d, const ICFGNode* icn) {
        return pag->addStoreStmt(s, d, icn);
    }
    GepStmt* addGepStmt(NodeID s, NodeID d, const AccessPath& ap, bool constGep) {
        return pag->addGepStmt(s, d, ap, constGep);
    }
    CallPE* addCallPE(NodeID s, NodeID d, const CallICFGNode* cs,
                      const FunEntryICFGNode* e) { return pag->addCallPE(s, d, cs, e); }
    RetPE* addRetPE(NodeID s, NodeID d, const CallICFGNode* cs,
                    const FunExitICFGNode* x) { return pag->addRetPE(s, d, cs, x); }

    SVFIR* getPAG() { return pag; }
private:
    SVFIR* pag;
};

class ICFGBuilder {
public:
    explicit ICFGBuilder(ICFG* g) : icfg(g) {}
    FunEntryICFGNode* addFunEntry(const FunObjVar* f) {
        return icfg->addFunEntryICFGNode(f);
    }
    FunExitICFGNode* addFunExit(const FunObjVar* f) {
        return icfg->addFunExitICFGNode(f);
    }
    IntraICFGNode* addIntra(const SVFBasicBlock* bb) {
        return icfg->addIntraICFGNode(bb, false);
    }
    void addIntraEdge(ICFGNode* a, ICFGNode* b) {
        if (a && b) icfg->addIntraEdge(a, b);
    }
    void addGlobal(GlobalICFGNode* g) { icfg->addGlobalICFGNode(g); }
    GlobalICFGNode* global() { return icfg->getGlobalICFGNode(); }
private:
    ICFG* icfg;
};

// Reach SVFType's protected static setters via a derived class.
class TypeSetter : public SVFOtherType {
public:
    TypeSetter() : SVFOtherType(0, true, 0) {}
    static void setPtr(SVFType* t) { SVFType::setSVFPtrType(t); }
    static void setI8 (SVFType* t) { SVFType::setSVFInt8Type(t); }
};

} // namespace SVF
