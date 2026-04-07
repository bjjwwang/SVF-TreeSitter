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
    void seq(SVF::ICFGNode* a, SVF::ICFGNode* c) { b.addIntraEdge(a, c); }
    SVF::GlobalICFGNode* global() { return b.global(); }

private:
    SVF::ICFGBuilder b;
};

} // namespace svfts
