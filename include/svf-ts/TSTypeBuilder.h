// TSTypeBuilder.h — translates tree-sitter-c types to SVFType
#pragma once
#include "SVFIR/SVFType.h"
#include "svf-ts/SimpleDataLayout.h"
#include <tree_sitter/api.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace svfts {

class TSTypeBuilder {
public:
    TSTypeBuilder();
    ~TSTypeBuilder();

    /// initial special types (void, ptr, i8, ...). Must be called once.
    void init();

    SVF::SVFType* getVoidType();
    SVF::SVFType* getPtrType();   // generic opaque pointer (SVF style)
    SVF::SVFType* getIntType(unsigned bytes);
    SVF::SVFType* getFloatType(bool isDouble);

    /// Look up or build a struct type by name; fields can be added afterwards.
    SVF::SVFStructType* getOrCreateStructType(const std::string& name);

    /// Build an array type
    SVF::SVFArrayType* getArrayType(SVF::SVFType* elem, unsigned numElems);

    /// Resolve typedef alias
    void addTypedef(const std::string& alias, SVF::SVFType* target);
    SVF::SVFType* lookupTypedef(const std::string& alias) const;

    /// Parse a tree-sitter `type` field together with declarator decorations
    /// (pointer/array). Returns a fresh SVFType pointer (owned by builder).
    SVF::SVFType* parseTypeFromNode(TSNode typeNode, const std::string& src);

    /// Wrap any type as `T*`
    SVF::SVFType* makePointer(SVF::SVFType* pointee);

    /// Build a flat StInfo for a type (idempotent).
    void buildStInfo(SVF::SVFType* ty);

    SimpleDataLayout& dl() { return layout; }

    /// All owned types — released on destruction.
    const std::vector<std::unique_ptr<SVF::SVFType>>& allTypes() const { return owned; }

private:
    SimpleDataLayout layout;
    std::vector<std::unique_ptr<SVF::SVFType>> owned;
    std::vector<std::unique_ptr<SVF::StInfo>> ownedStInfo;
    std::unordered_map<std::string, SVF::SVFType*> typedefs;
    std::unordered_map<std::string, SVF::SVFStructType*> structs;
    SVF::SVFType* voidTy = nullptr;
    SVF::SVFType* ptrTy = nullptr;
    SVF::SVFIntegerType* intTys[9] = {nullptr}; // index by byte width
    SVF::SVFType* floatTy = nullptr;
    SVF::SVFType* doubleTy = nullptr;

    template <typename T, typename... A>
    T* own(A&&... a) {
        auto p = std::make_unique<T>(std::forward<A>(a)...);
        T* raw = p.get();
        owned.emplace_back(std::move(p));
        return raw;
    }
    SVF::StInfo* ownStInfo(SVF::StInfo* p) {
        ownedStInfo.emplace_back(p);
        return p;
    }
    unsigned nextTypeId = 100; // SVFType ids; opaque to us
};

} // namespace svfts
