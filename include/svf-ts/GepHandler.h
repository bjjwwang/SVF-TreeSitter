// GepHandler.h — generates GepStmts following GEP_COMPLETE_GUIDE
#pragma once
#include "SVFIR/SVFIR.h"
#include "MemoryModel/AccessPath.h"
#include "svf-ts/SimpleDataLayout.h"

namespace svfts {

class GepHandler {
public:
    GepHandler(SVF::SVFIR* pag, SimpleDataLayout* dl) : pag(pag), dl(dl) {}

    /// Add a GepStmt for a constant flattened field index.
    /// Returns the dst NodeID (a fresh ValVar created internally).
    SVF::NodeID addConstFieldGep(SVF::NodeID base, unsigned flatFldIdx,
                                 const SVF::SVFType* baseType,
                                 const SVF::ICFGNode* icfgNode);

    /// Add a variant (unknown offset) GepStmt.
    SVF::NodeID addVariantGep(SVF::NodeID base, const SVF::SVFType* baseType,
                              const SVF::ICFGNode* icfgNode);

private:
    SVF::SVFIR* pag;
    SimpleDataLayout* dl;
};

} // namespace svfts
