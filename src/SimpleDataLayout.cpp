#include "svf-ts/SimpleDataLayout.h"
#include "Util/SVFUtil.h"

using namespace SVF;

namespace svfts {

size_t SimpleDataLayout::getTypeSize(const SVFType* ty) const {
    if (!ty) return 0;
    if (ty->isPointerTy()) return 8;
    if (auto* it = SVFUtil::dyn_cast<SVFIntegerType>(ty)) {
        // SVFType byteSize stores the byte size for sized types.
        unsigned bs = it->getByteSize();
        return bs ? bs : 4;
    }
    if (auto* st = SVFUtil::dyn_cast<SVFStructType>(ty)) {
        size_t total = 0;
        const auto& fields = st->getFieldTypes();
        for (size_t i = 0; i < fields.size(); ++i) {
            total += getTypeSize(fields[i]);
            if (i + 1 < fields.size())
                total = alignTo(total, getTypeAlignment(fields[i + 1]));
        }
        return total;
    }
    if (auto* at = SVFUtil::dyn_cast<SVFArrayType>(ty)) {
        const SVFType* et = at->getTypeOfElement();
        // SVFArrayType doesn't expose count via public API; rely on byteSize.
        unsigned bs = ty->getByteSize();
        if (bs) return bs;
        return getTypeSize(et);
    }
    // floats / others: rely on stored byteSize
    return ty->getByteSize();
}

size_t SimpleDataLayout::getTypeAlignment(const SVFType* ty) const {
    if (!ty) return 1;
    if (ty->isPointerTy()) return 8;
    if (auto* it = SVFUtil::dyn_cast<SVFIntegerType>(ty)) {
        unsigned bs = it->getByteSize();
        return bs ? std::min<size_t>(bs, 8) : 4;
    }
    if (auto* st = SVFUtil::dyn_cast<SVFStructType>(ty)) {
        size_t a = 1;
        for (auto* f : st->getFieldTypes())
            a = std::max(a, getTypeAlignment(f));
        return a;
    }
    if (auto* at = SVFUtil::dyn_cast<SVFArrayType>(ty)) {
        return getTypeAlignment(at->getTypeOfElement());
    }
    return std::min<size_t>(ty->getByteSize() ? ty->getByteSize() : 1, 8);
}

size_t SimpleDataLayout::getFieldByteOffset(const SVFStructType* st, unsigned fieldIdx) const {
    size_t offset = 0;
    const auto& fields = st->getFieldTypes();
    for (unsigned i = 0; i < fieldIdx && i < fields.size(); ++i) {
        offset += getTypeSize(fields[i]);
        if (i + 1 < fields.size())
            offset = alignTo(offset, getTypeAlignment(fields[i + 1]));
    }
    return offset;
}

int SimpleDataLayout::byteOffsetToFieldIdx(const SVFStructType* st, size_t byteOffset) const {
    size_t offset = 0;
    const auto& fields = st->getFieldTypes();
    for (unsigned i = 0; i < fields.size(); ++i) {
        if (offset == byteOffset) return (int)i;
        offset += getTypeSize(fields[i]);
        if (i + 1 < fields.size())
            offset = alignTo(offset, getTypeAlignment(fields[i + 1]));
    }
    return -1;
}

} // namespace svfts
