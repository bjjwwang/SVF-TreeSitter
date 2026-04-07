#include "svf-ts/TSTypeBuilder.h"
#include "svf-ts/SvfShim.h"
#include "Util/SVFUtil.h"
#include <cstring>
#include <functional>

using namespace SVF;

namespace svfts {

static std::string nodeText(TSNode n, const std::string& src) {
    return src.substr(ts_node_start_byte(n), ts_node_end_byte(n) - ts_node_start_byte(n));
}

TSTypeBuilder::TSTypeBuilder() {}
TSTypeBuilder::~TSTypeBuilder() {}

void TSTypeBuilder::init() {
    voidTy = own<SVFOtherType>(nextTypeId++, false, 0);
    static_cast<SVFOtherType*>(voidTy)->setRepr("void");
    ptrTy = own<SVFPointerType>(nextTypeId++, 8);
    TypeSetter::setPtr(ptrTy);
    for (unsigned w : {1u, 2u, 4u, 8u}) {
        auto* it = own<SVFIntegerType>(nextTypeId++, w);
        intTys[w] = it;
        if (w == 1) TypeSetter::setI8(it);
        buildStInfo(it);
    }
    floatTy = own<SVFOtherType>(nextTypeId++, true, 4);
    static_cast<SVFOtherType*>(floatTy)->setRepr("float");
    doubleTy = own<SVFOtherType>(nextTypeId++, true, 8);
    static_cast<SVFOtherType*>(doubleTy)->setRepr("double");
    buildStInfo(voidTy);
    buildStInfo(ptrTy);
    buildStInfo(floatTy);
    buildStInfo(doubleTy);
}

SVFType* TSTypeBuilder::getVoidType() { return voidTy; }
SVFType* TSTypeBuilder::getPtrType() { return ptrTy; }
SVFType* TSTypeBuilder::getIntType(unsigned bytes) {
    if (bytes < 1 || bytes > 8) bytes = 4;
    // round up to power of two we have
    while (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8) ++bytes;
    return intTys[bytes];
}
SVFType* TSTypeBuilder::getFloatType(bool isDouble) {
    return isDouble ? doubleTy : floatTy;
}

SVFStructType* TSTypeBuilder::getOrCreateStructType(const std::string& name) {
    auto it = structs.find(name);
    if (it != structs.end()) return it->second;
    std::vector<const SVFType*> emptyFields;
    auto* st = own<SVFStructType>(nextTypeId++, emptyFields, 0);
    st->setName(name);
    structs[name] = st;
    return st;
}

SVFArrayType* TSTypeBuilder::getArrayType(SVFType* elem, unsigned numElems) {
    auto* at = own<SVFArrayType>(nextTypeId++, numElems * (unsigned)layout.getTypeSize(elem));
    at->setTypeOfElement(elem);
    at->setNumOfElement(numElems);
    buildStInfo(at);
    return at;
}

void TSTypeBuilder::addTypedef(const std::string& alias, SVFType* target) {
    typedefs[alias] = target;
}

SVFType* TSTypeBuilder::lookupTypedef(const std::string& alias) const {
    auto it = typedefs.find(alias);
    return it == typedefs.end() ? nullptr : it->second;
}

SVFType* TSTypeBuilder::makePointer(SVFType* /*pointee*/) {
    // SVF uses an opaque pointer model — single shared pointer type.
    return ptrTy;
}

SVFType* TSTypeBuilder::parseTypeFromNode(TSNode typeNode, const std::string& src) {
    if (ts_node_is_null(typeNode)) return getIntType(4);
    const char* kind = ts_node_type(typeNode);
    std::string text = nodeText(typeNode, src);
    if (strcmp(kind, "primitive_type") == 0 || strcmp(kind, "sized_type_specifier") == 0) {
        if (text.find("char") != std::string::npos) return getIntType(1);
        if (text.find("short") != std::string::npos) return getIntType(2);
        if (text.find("long long") != std::string::npos) return getIntType(8);
        if (text.find("long") != std::string::npos) return getIntType(8);
        if (text.find("double") != std::string::npos) return getFloatType(true);
        if (text.find("float") != std::string::npos) return getFloatType(false);
        if (text.find("void") != std::string::npos) return getVoidType();
        if (text.find("_Bool") != std::string::npos || text.find("bool") != std::string::npos)
            return getIntType(1);
        return getIntType(4);
    }
    if (strcmp(kind, "type_identifier") == 0) {
        if (auto* t = lookupTypedef(text)) return t;
        return getIntType(4);
    }
    if (strcmp(kind, "struct_specifier") == 0 || strcmp(kind, "union_specifier") == 0) {
        TSNode nameN = ts_node_child_by_field_name(typeNode, "name", 4);
        std::string sname = ts_node_is_null(nameN) ? std::string("anon_struct_") + std::to_string(nextTypeId)
                                                   : nodeText(nameN, src);
        return getOrCreateStructType(sname);
    }
    if (strcmp(kind, "enum_specifier") == 0) return getIntType(4);
    return getIntType(4);
}

void TSTypeBuilder::buildStInfo(SVFType* ty) {
    if (!ty) return;
    if (auto* st = SVFUtil::dyn_cast<SVFStructType>(ty)) {
        // Flatten struct fields recursively
        auto* info = ownStInfo(new StInfo(1));
        std::vector<const SVFType*>& flat = info->getFlattenFieldTypes();
        std::vector<u32_t>& fldIdx = info->getFlattenedFieldIdxVec();
        std::function<void(const SVFType*)> flatten = [&](const SVFType* t) {
            if (!t) return;
            if (auto* sub = SVFUtil::dyn_cast<SVFStructType>(t)) {
                for (auto* f : sub->getFieldTypes()) flatten(f);
            } else if (auto* ar = SVFUtil::dyn_cast<SVFArrayType>(t)) {
                flatten(ar->getTypeOfElement());
            } else {
                fldIdx.push_back((u32_t)flat.size());
                flat.push_back(t);
            }
        };
        for (auto* f : st->getFieldTypes()) flatten(f);
        if (flat.empty()) flat.push_back(ty);
        info->setNumOfFieldsAndElems((u32_t)flat.size(), (u32_t)flat.size());
        ty->setTypeInfo(info);
        return;
    }
    if (auto* ar = SVFUtil::dyn_cast<SVFArrayType>(ty)) {
        auto* info = ownStInfo(new StInfo(1));
        const SVFType* et = ar->getTypeOfElement();
        info->getFlattenFieldTypes().push_back(et ? et : ty);
        info->getFlattenedFieldIdxVec().push_back(0);
        info->setNumOfFieldsAndElems(1, 1);
        ty->setTypeInfo(info);
        return;
    }
    // scalar / pointer / other: 1 field == self
    auto* info = ownStInfo(new StInfo(1));
    info->getFlattenFieldTypes().push_back(ty);
    info->getFlattenedFieldIdxVec().push_back(0);
    info->setNumOfFieldsAndElems(1, 1);
    ty->setTypeInfo(info);
}

} // namespace svfts
