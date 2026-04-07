#include "svf-ts/GepHandler.h"
#include "svf-ts/SvfShim.h"
#include "Util/NodeIDAllocator.h"
#include "SVFIR/SVFType.h"

using namespace SVF;

namespace svfts {

NodeID GepHandler::addConstFieldGep(NodeID base, unsigned flatFldIdx,
                                    const SVFType* baseType,
                                    const ICFGNode* icfgNode) {
    SVFIRBuilder b(pag);
    NodeID dst = NodeIDAllocator::get()->allocateValueId();
    b.addValNode(dst, SVFType::getSVFPtrType(), icfgNode);
    AccessPath ap((APOffset)flatFldIdx, baseType);
    b.addGepStmt(base, dst, ap, /*constantOffset*/ true);
    return dst;
}

NodeID GepHandler::addVariantGep(NodeID base, const SVFType* baseType,
                                 const ICFGNode* icfgNode) {
    SVFIRBuilder b(pag);
    NodeID dst = NodeIDAllocator::get()->allocateValueId();
    b.addValNode(dst, SVFType::getSVFPtrType(), icfgNode);
    AccessPath ap(0, baseType);
    b.addGepStmt(base, dst, ap, /*constantOffset*/ false);
    return dst;
}

} // namespace svfts
