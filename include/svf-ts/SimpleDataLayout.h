// SimpleDataLayout.h — hardcoded x86-64 LP64 layout
#pragma once
#include "SVFIR/SVFType.h"
#include <vector>

namespace svfts {

class SimpleDataLayout {
public:
    /// byte size of a type (LP64)
    size_t getTypeSize(const SVF::SVFType* ty) const;
    /// alignment requirement of a type
    size_t getTypeAlignment(const SVF::SVFType* ty) const;
    /// byte offset of struct field N
    size_t getFieldByteOffset(const SVF::SVFStructType* st, unsigned fieldIdx) const;
    /// convert a byte offset into a flattened field index, -1 if not aligned
    int byteOffsetToFieldIdx(const SVF::SVFStructType* st, size_t byteOffset) const;
    /// align offset upwards to multiple of align
    static size_t alignTo(size_t offset, size_t align) {
        if (align == 0) return offset;
        return (offset + align - 1) / align * align;
    }
};

} // namespace svfts
