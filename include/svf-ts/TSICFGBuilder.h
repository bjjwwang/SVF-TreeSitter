// TSICFGBuilder.h — incrementally build ICFG nodes & edges
#pragma once
#include "svf-ts/SvfShim.h"

namespace svfts {

class TSICFGBuilder {
public:
    explicit TSICFGBuilder(SVF::ICFG* icfg) : b(icfg) {}
    SVF::FunEntryICFGNode* addFunEntry(const SVF::FunObjVar* f) { return b.addFunEntry(f); }
    SVF::FunExitICFGNode*  addFunExit (const SVF::FunObjVar* f) { return b.addFunExit(f); }
    SVF::IntraICFGNode*    addIntra(const SVF::SVFBasicBlock* bb) { return b.addIntra(bb); }
    SVF::CallICFGNode* addCall(const SVF::SVFBasicBlock* bb, const SVF::SVFType* ty,
                               const SVF::FunObjVar* callee, bool vararg = false) {
        return b.addCall(bb, ty, callee, vararg);
    }
    SVF::RetICFGNode* addRet(SVF::CallICFGNode* c) { return b.addRet(c); }
    void seq(SVF::ICFGNode* a, SVF::ICFGNode* c) { b.addIntraEdge(a, c); }
    void callEdge(SVF::ICFGNode* a, SVF::ICFGNode* c) { b.addCallEdge(a, c); }
    void retEdge(SVF::ICFGNode* a, SVF::ICFGNode* c)  { b.addRetEdge(a, c); }
    SVF::GlobalICFGNode* global() { return b.global(); }

private:
    SVF::ICFGBuilder b;
};

} // namespace svfts
