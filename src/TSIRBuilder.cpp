// TSIRBuilder.cpp — visit tree-sitter-c CST and build SVFIR
//
// This is a *first cut* covering the patterns from the docs that the basic
// test suite exercises (addr, copy, load, store, simple struct, simple call,
// return, if/while/for skeletal control flow). It is intentionally
// conservative — anything it doesn't understand falls back to a fresh ValVar
// pointing at BlackHole, which is sound but imprecise.

#include "svf-ts/TSIRBuilder.h"
#include "svf-ts/SvfShim.h"
#include "Util/NodeIDAllocator.h"
#include "Util/SVFUtil.h"
#include "SVFIR/ObjTypeInfo.h"
#include "Graphs/ICFGNode.h"
#include "Graphs/BasicBlockG.h"
#include "Util/SVFLoopAndDomInfo.h"
#include "Graphs/CallGraph.h"
#include <cassert>
#include <cstring>
#include <functional>

#define B SVF::SVFIRBuilder(pag)

using namespace SVF;

namespace svfts {

// ---------------- ctor / specials ----------------

TSIRBuilder::TSIRBuilder(SVFIR* p, TSTypeBuilder* t, TSSymbolTable* s,
                         TSICFGBuilder* ic, GepHandler* g)
    : pag(p), tb(t), st(s), icfgB(ic), gep(g) {}

void TSIRBuilder::initSpecials() {
    // BlackHole and Constant nodes are typically initialised by SVFIR ctor;
    // this is just a sanity hook.
}

// ---------------- helpers: alloc nodes ----------------

NodeID TSIRBuilder::freshValVar(const SVFType* ty) {
    NodeID id = NodeIDAllocator::get()->allocateValueId();
    const SVFType* eff = ty ? ty : tb->getPtrType();
    B.addValNode(id, eff, currentICFG);
    return id;
}

NodeID TSIRBuilder::freshObjVar(const SVFType* ty, unsigned flag) {
    NodeID id = NodeIDAllocator::get()->allocateObjectId();
    auto* info = new ObjTypeInfo(ty ? ty : SVFType::getSVFPtrType(),
                                 ty && ty->getTypeInfo() ? ty->getTypeInfo()->getNumOfFlattenFields() : 1);
    info->setFlag((ObjTypeInfo::MEMTYPE)flag);
    info->setByteSizeOfObj((u32_t)tb->dl().getTypeSize(ty));
    if (flag & ObjTypeInfo::STACK_OBJ)       B.addStackObjNode(id, info, currentICFG);
    else if (flag & ObjTypeInfo::HEAP_OBJ)   B.addHeapObjNode(id, info, currentICFG);
    else if (flag & ObjTypeInfo::GLOBVAR_OBJ)B.addGlobalObjNode(id, info, currentICFG);
    else                                     B.addObjNode(id, info, currentICFG);
    return id;
}

void TSIRBuilder::nameNode(NodeID id, const std::string& name) {
    if (!pag->hasGNode(id)) return;
    if (auto* v = pag->getGNode(id)) v->setName(name);
}

Symbol TSIRBuilder::createLocal(const std::string& name, const SVFType* ty) {
    Symbol s;
    s.type = ty;
    s.objId = freshObjVar(ty, ObjTypeInfo::STACK_OBJ);
    s.valId = freshValVar(SVFType::getSVFPtrType());
    nameNode(s.objId, name + ".addr");
    nameNode(s.valId, name);
    B.addAddrStmt(s.objId, s.valId);
    st->addLocal(name, s);
    return s;
}

Symbol TSIRBuilder::createGlobal(const std::string& name, const SVFType* ty) {
    Symbol s;
    s.type = ty;
    s.objId = freshObjVar(ty, ObjTypeInfo::GLOBVAR_OBJ);
    s.valId = freshValVar(SVFType::getSVFPtrType());
    nameNode(s.objId, name + ".addr");
    nameNode(s.valId, name);
    B.addAddrStmt(s.objId, s.valId);
    st->addGlobal(name, s);
    return s;
}

int TSIRBuilder::findFieldMetaIdx(const SVFStructType* sty,
                                  const std::string& fname) const {
    auto it = structFields.find(sty);
    if (it == structFields.end()) return -1;
    for (size_t i = 0; i < it->second.size(); ++i)
        if (it->second[i].name == fname) return (int)i;
    return -1;
}

// ---------------- top-level driver ----------------

void TSIRBuilder::visitTranslationUnit(TSNode root, const std::string& src) {
    // Use the ICFG via the builder rather than pag->getICFG() (the latter
    // asserts totalICFGNode>0 which isn't true for a global node only).
    currentICFG = icfgB->global();
    st->enterScope();
    uint32_t n = ts_node_child_count(root);
    // Pre-pass: register every function signature first so visitCall can
    // resolve forward references (and so visitFunctionDef just fills bodies).
    for (uint32_t i = 0; i < n; ++i) {
        TSNode c = ts_node_child(root, i);
        if (strcmp(ts_node_type(c), "function_definition") == 0)
            prescanFunctionDef(c, src);
    }
    // Main pass.
    for (uint32_t i = 0; i < n; ++i) {
        TSNode c = ts_node_child(root, i);
        visitTopLevelDecl(c, src);
    }
    st->exitScope();
}

void TSIRBuilder::finalize() {}

void TSIRBuilder::visitTopLevelDecl(TSNode n, const std::string& src) {
    const char* k = ts_node_type(n);
    if (strcmp(k, "function_definition") == 0)      visitFunctionDef(n, src);
    else if (strcmp(k, "declaration") == 0)          visitGlobalDecl(n, src);
    else if (strcmp(k, "struct_specifier") == 0 ||
             strcmp(k, "union_specifier") == 0) {
        // forward / standalone struct decl: register fields
        TSNode nameN = ts_node_child_by_field_name(n, "name", 4);
        TSNode body  = ts_node_child_by_field_name(n, "body", 4);
        if (ts_node_is_null(body)) return;
        std::string sname = ts_node_is_null(nameN) ? std::string("anon") : text(nameN, src);
        SVFStructType* sty = tb->getOrCreateStructType(sname);
        std::vector<FieldMeta>& fmetas = structFields[sty];
        std::vector<const SVFType*> fields;
        uint32_t bn = ts_node_child_count(body);
        for (uint32_t i = 0; i < bn; ++i) {
            TSNode fd = ts_node_child(body, i);
            if (strcmp(ts_node_type(fd), "field_declaration") != 0) continue;
            TSNode tnode = ts_node_child_by_field_name(fd, "type", 4);
            TSNode dnode = ts_node_child_by_field_name(fd, "declarator", 10);
            SVFType* ft = tb->parseTypeFromNode(tnode, src);
            // Remember whether the *source* type is a struct (before we
            // erase it to getPtrType for pointer-decorated declarators).
            const SVFStructType* fieldStructTy = SVFUtil::dyn_cast<SVFStructType>(ft);
            std::string fname = ts_node_is_null(dnode) ? "" : text(dnode, src);
            bool isPtr = (fname.find('*') != std::string::npos);
            if (isPtr) ft = tb->getPtrType();
            while (!fname.empty() && (fname.front() == '*' || fname.front() == '&'))
                fname.erase(fname.begin());
            auto br = fname.find('[');
            if (br != std::string::npos) fname.erase(br);
            fields.push_back(ft);
            FieldMeta fm;
            fm.name      = fname;
            fm.isPointer = isPtr;
            fm.pointee   = fieldStructTy; // non-null for X or X*
            fmetas.push_back(std::move(fm));
        }
        // Note: SVFStructType API only supports adding via friend; we
        // approximate by recreating with fields. Since the same name is
        // looked up later, downstream code uses our parallel `fnames` map.
        // (StInfo is rebuilt below.)
        // Workaround: rebuild StInfo as a flat list whose size matches.
        auto* info = new StInfo(1);
        for (size_t i = 0; i < fields.size(); ++i) {
            info->getFlattenedFieldIdxVec().push_back((u32_t)i);
            info->getFlattenFieldTypes().push_back(fields[i]);
        }
        info->setNumOfFieldsAndElems((u32_t)fields.size(), (u32_t)fields.size());
        sty->setTypeInfo(info);
    }
    else if (strcmp(k, "type_definition") == 0) {
        // typedef T name;
        TSNode tnode = ts_node_child_by_field_name(n, "type", 4);
        TSNode dnode = ts_node_child_by_field_name(n, "declarator", 10);
        if (ts_node_is_null(tnode) || ts_node_is_null(dnode)) return;
        SVFType* base = tb->parseTypeFromNode(tnode, src);
        std::string aliasName = text(dnode, src);
        while (!aliasName.empty() && (aliasName.front() == '*' || aliasName.front() == ' '))
            aliasName.erase(aliasName.begin());
        tb->addTypedef(aliasName, base);
    }
}

// ---------------- function definition ----------------

namespace {
// Dig the first identifier child under a subtree — used to extract a function
// name from nested (pointer_)declarator wrappers.
std::string digIdent(TSNode x, const std::string& src) {
    if (ts_node_is_null(x)) return {};
    if (strcmp(ts_node_type(x), "identifier") == 0)
        return std::string(src.c_str() + ts_node_start_byte(x),
                           ts_node_end_byte(x) - ts_node_start_byte(x));
    for (uint32_t i = 0; i < ts_node_child_count(x); ++i) {
        std::string r = digIdent(ts_node_child(x, i), src);
        if (!r.empty()) return r;
    }
    return {};
}
TSNode findParamList(TSNode x) {
    if (ts_node_is_null(x)) return TSNode{};
    if (strcmp(ts_node_type(x), "parameter_list") == 0) return x;
    for (uint32_t i = 0; i < ts_node_child_count(x); ++i) {
        TSNode r = findParamList(ts_node_child(x, i));
        if (!ts_node_is_null(r) && strcmp(ts_node_type(r), "parameter_list") == 0) return r;
    }
    return TSNode{};
}
} // namespace

void TSIRBuilder::prescanFunctionDef(TSNode n, const std::string& src) {
    TSNode declN = ts_node_child_by_field_name(n, "declarator", 10);
    TSNode bodyN = ts_node_child_by_field_name(n, "body", 4);
    TSNode retTN = ts_node_child_by_field_name(n, "type", 4);
    if (ts_node_is_null(declN) || ts_node_is_null(bodyN)) return;

    std::string fname = digIdent(declN, src);
    if (fname.empty()) return;
    if (funcInfos.count(fname)) return;  // already registered

    FuncInfo fi;
    // A pointer return type manifests as a `pointer_declarator` wrapping
    // the `function_declarator` at the top of declN.
    fi.retIsPtr = (strcmp(ts_node_type(declN), "pointer_declarator") == 0);

    SVFType* retT = tb->parseTypeFromNode(retTN, src);
    std::vector<const SVFType*> ptys;
    fi.type = new SVFFunctionType(0, retT, ptys, false);
    tb->buildStInfo(fi.type);

    // FunObj
    fi.funObjId = NodeIDAllocator::get()->allocateObjectId();
    auto* info = new ObjTypeInfo(fi.type, 1);
    info->setFlag(ObjTypeInfo::FUNCTION_OBJ);
    info->setByteSizeOfObj(1);
    B.addFunObjNode(fi.funObjId, info, nullptr);
    fi.fun = const_cast<FunObjVar*>(SVFUtil::dyn_cast<FunObjVar>(pag->getGNode(fi.funObjId)));
    if (!fi.fun) return;

    // BasicBlockGraph / LoopAndDomInfo / dummy BBs so getFunction() works.
    fi.bbg = new BasicBlockGraph();
    auto* ld = new SVFLoopAndDomInfo();
    auto makeBB = [&](const std::string& nm) -> SVFBasicBlock* {
        fi.bbg->id++;
        auto* bb = new SVFBasicBlock(fi.bbg->id, fi.fun);
        bb->setName(nm);
        fi.bbg->addBasicBlock(bb);
        return bb;
    };
    fi.exitBB  = makeBB("exit");
    fi.fun->initFunObjVar(false, false, false, false, false, false,
                          fi.type, ld, fi.fun, fi.bbg, {}, fi.exitBB);
    fi.entryBB = makeBB("entry");

    const_cast<CallGraph*>(pag->getCallGraph())->addCallGraphNode(fi.fun);

    fi.entry = icfgB->addFunEntry(fi.fun);
    fi.exit  = icfgB->addFunExit(fi.fun);

    // Register function as a global symbol (takes its address).
    {
        Symbol fs;
        fs.type = fi.type;
        fs.objId = fi.funObjId;
        // We want the function-pointer ValVar anchored at the global ICFG node.
        NodeID id = NodeIDAllocator::get()->allocateValueId();
        B.addValNode(id, SVFType::getSVFPtrType(), icfgB->global());
        fs.valId = id;
        nameNode(id, fname);
        B.addAddrStmt(fi.funObjId, fs.valId);
        fs.isFunction = true;
        st->addGlobal(fname, fs);
    }

    // Create formal ValVars anchored at the function entry.
    TSNode paramsN = findParamList(declN);
    if (!ts_node_is_null(paramsN)) {
        uint32_t pn = ts_node_named_child_count(paramsN);
        for (uint32_t i = 0; i < pn; ++i) {
            TSNode p = ts_node_named_child(paramsN, i);
            if (strcmp(ts_node_type(p), "parameter_declaration") != 0) continue;
            TSNode pT = ts_node_child_by_field_name(p, "type", 4);
            TSNode pD = ts_node_child_by_field_name(p, "declarator", 10);
            SVFType* pty = tb->parseTypeFromNode(pT, src);
            const SVFStructType* pstruct = SVFUtil::dyn_cast<SVFStructType>(pty);
            std::string pname = ts_node_is_null(pD)
                                ? std::string("arg") + std::to_string(i)
                                : text(pD, src);
            while (!pname.empty() && (pname.front() == '*' || pname.front() == ' '))
                pname.erase(pname.begin());
            auto br = pname.find('[');
            if (br != std::string::npos) pname.erase(br);
            NodeID fv = NodeIDAllocator::get()->allocateValueId();
            B.addValNode(fv, SVFType::getSVFPtrType(), fi.entry);
            nameNode(fv, pname);
            fi.formals.push_back(fv);
            fi.formalNames.push_back(pname);
            fi.formalStructTys.push_back(pstruct);
        }
    }

    // Return ValVar (callee-owned) anchored at the function exit.
    fi.retVal = NodeIDAllocator::get()->allocateValueId();
    B.addValNode(fi.retVal, SVFType::getSVFPtrType(), fi.exit);
    nameNode(fi.retVal, fname + ".ret");

    funcInfos.emplace(fname, fi);
}

void TSIRBuilder::visitFunctionDef(TSNode n, const std::string& src) {
    TSNode declN = ts_node_child_by_field_name(n, "declarator", 10);
    TSNode bodyN = ts_node_child_by_field_name(n, "body", 4);
    if (ts_node_is_null(declN) || ts_node_is_null(bodyN)) return;

    std::string fname = digIdent(declN, src);
    if (fname.empty()) return;
    auto it = funcInfos.find(fname);
    if (it == funcInfos.end()) return;
    FuncInfo& fi = it->second;
    if (fi.defined) return;
    fi.defined = true;

    currentFunc     = fi.fun;
    currentBB       = fi.entryBB;
    currentRetVal   = fi.retVal;
    currentRetIsPtr = fi.retIsPtr;

    // Body executes in an intra node chained from entry.
    currentICFG = icfgB->addIntra(currentBB);
    icfgB->seq(fi.entry, currentICFG);

    // Bind each formal to a backing stack slot, then store the incoming
    // formal value into the slot. This mirrors clang's alloca-based
    // parameter ABI so reads of `p` inside the body go through a Load
    // (preserving the p->{p, ...} self-object pattern Andersen sees from
    // the LLVM frontend) while CallPE still targets the bare formal.
    st->enterScope();
    for (size_t i = 0; i < fi.formals.size(); ++i) {
        Symbol s = createLocal(fi.formalNames[i], tb->getPtrType());
        B.addStoreStmt(fi.formals[i], s.valId, currentICFG);
        // If the parameter is declared as `struct X` or `struct X*`, the
        // slot's identifier reads should resolve to a struct type for
        // downstream field access.
        if (i < fi.formalStructTys.size() && fi.formalStructTys[i]) {
            tagStruct(s.valId,          fi.formalStructTys[i]);
            tagStruct(fi.formals[i],    fi.formalStructTys[i]);
        }
    }

    visitStmt(bodyN, src);
    icfgB->seq(currentICFG, fi.exit);

    st->exitScope();
    currentFunc     = nullptr;
    currentICFG     = nullptr;
    currentRetVal   = 0;
    currentRetIsPtr = false;
}

// ---------------- global declaration ----------------

void TSIRBuilder::visitGlobalDecl(TSNode n, const std::string& src) {
    TSNode tnode = ts_node_child_by_field_name(n, "type", 4);
    SVFType* ty = tb->parseTypeFromNode(tnode, src);
    const SVFStructType* declStructTy = SVFUtil::dyn_cast<SVFStructType>(ty);
    uint32_t cn = ts_node_child_count(n);
    for (uint32_t i = 0; i < cn; ++i) {
        TSNode c = ts_node_child(n, i);
        const char* k = ts_node_type(c);
        if (strcmp(k, "init_declarator") == 0 || strcmp(k, "identifier") == 0 ||
            strcmp(k, "pointer_declarator") == 0 || strcmp(k, "array_declarator") == 0) {
            std::string name;
            std::function<void(TSNode)> dig = [&](TSNode x) {
                if (!name.empty()) return;
                if (strcmp(ts_node_type(x), "identifier") == 0) {
                    name = text(x, src); return;
                }
                for (uint32_t j = 0; j < ts_node_child_count(x); ++j) dig(ts_node_child(x, j));
            };
            dig(c);
            if (name.empty()) continue;
            SVFType* eff = ty;
            if (strcmp(k, "pointer_declarator") == 0 || text(c, src).find('*') != std::string::npos)
                eff = tb->getPtrType();
            Symbol s = createGlobal(name, eff);
            if (declStructTy) tagStruct(s.valId, declStructTy);
            if (strcmp(k, "array_declarator") == 0 ||
                text(c, src).find('[') != std::string::npos)
                if (auto* m = st->lookupMutable(name)) m->isArray = true;
            // Process initializer if present (init_declarator only)
            if (strcmp(k, "init_declarator") == 0) {
                TSNode valN = ts_node_child_by_field_name(c, "value", 5);
                if (!ts_node_is_null(valN)) {
                    NodeID rhs = visitExpr(valN, src);
                    B.addStoreStmt(rhs, s.valId, currentICFG);
                }
            }
        }
    }
}

// ---------------- statements ----------------

void TSIRBuilder::visitStmt(TSNode n, const std::string& src) {
    if (ts_node_is_null(n)) return;
    const char* k = ts_node_type(n);
    if (strcmp(k, "compound_statement") == 0)        visitCompound(n, src);
    else if (strcmp(k, "declaration") == 0)          visitLocalDecl(n, src);
    else if (strcmp(k, "expression_statement") == 0) visitExprStmt(n, src);
    else if (strcmp(k, "return_statement") == 0)     visitReturn(n, src);
    else if (strcmp(k, "if_statement") == 0)         visitIf(n, src);
    else if (strcmp(k, "while_statement") == 0)      visitWhile(n, src);
    else if (strcmp(k, "for_statement") == 0)        visitFor(n, src);
    else {
        // generic walk
        for (uint32_t i = 0; i < ts_node_named_child_count(n); ++i)
            visitStmt(ts_node_named_child(n, i), src);
    }
}

void TSIRBuilder::visitCompound(TSNode n, const std::string& src) {
    st->enterScope();
    uint32_t cn = ts_node_named_child_count(n);
    for (uint32_t i = 0; i < cn; ++i)
        visitStmt(ts_node_named_child(n, i), src);
    st->exitScope();
}

void TSIRBuilder::visitLocalDecl(TSNode n, const std::string& src) {
    TSNode tnode = ts_node_child_by_field_name(n, "type", 4);
    SVFType* baseT = tb->parseTypeFromNode(tnode, src);
    // If the declared *source* type is a struct (or pointer-to-struct),
    // remember it so field accesses on this variable can resolve indices.
    // Note: for `struct X *p` visitLocalDecl still sees `struct X` as the
    // base type (the declarator contributes the `*`), so capturing baseT
    // before the pointer-stripping below is correct.
    const SVFStructType* declStructTy =
        SVFUtil::dyn_cast<SVFStructType>(baseT);
    uint32_t cn = ts_node_child_count(n);
    for (uint32_t i = 0; i < cn; ++i) {
        TSNode c = ts_node_child(n, i);
        const char* k = ts_node_type(c);
        if (strcmp(k, "init_declarator") == 0) {
            TSNode declN = ts_node_child_by_field_name(c, "declarator", 10);
            TSNode valN  = ts_node_child_by_field_name(c, "value", 5);
            std::string name;
            std::function<void(TSNode)> dig = [&](TSNode x) {
                if (!name.empty()) return;
                if (strcmp(ts_node_type(x), "identifier") == 0) { name = text(x, src); return; }
                for (uint32_t j = 0; j < ts_node_child_count(x); ++j) dig(ts_node_child(x, j));
            };
            dig(declN);
            if (name.empty()) continue;
            SVFType* eff = baseT;
            if (text(declN, src).find('*') != std::string::npos) eff = tb->getPtrType();
            bool declIsArray = text(declN, src).find('[') != std::string::npos;
            Symbol s = createLocal(name, eff);
            if (declStructTy) tagStruct(s.valId, declStructTy);
            if (declIsArray)
                if (auto* m = st->lookupMutable(name)) m->isArray = true;
            if (!ts_node_is_null(valN)) {
                NodeID rhs = visitExpr(valN, src);
                B.addStoreStmt(rhs, s.valId, currentICFG);
            }
        } else if (strcmp(k, "identifier") == 0 ||
                   strcmp(k, "pointer_declarator") == 0 ||
                   strcmp(k, "array_declarator") == 0) {
            std::string name;
            std::function<void(TSNode)> dig = [&](TSNode x) {
                if (!name.empty()) return;
                if (strcmp(ts_node_type(x), "identifier") == 0) { name = text(x, src); return; }
                for (uint32_t j = 0; j < ts_node_child_count(x); ++j) dig(ts_node_child(x, j));
            };
            dig(c);
            if (name.empty()) continue;
            SVFType* eff = baseT;
            if (strcmp(k, "pointer_declarator") == 0) eff = tb->getPtrType();
            bool bareIsArray = (strcmp(k, "array_declarator") == 0) ||
                               (text(c, src).find('[') != std::string::npos);
            Symbol s2 = createLocal(name, eff);
            if (declStructTy) tagStruct(s2.valId, declStructTy);
            if (bareIsArray)
                if (auto* m = st->lookupMutable(name)) m->isArray = true;
        }
    }
}

void TSIRBuilder::visitExprStmt(TSNode n, const std::string& src) {
    if (ts_node_named_child_count(n) > 0)
        visitExpr(ts_node_named_child(n, 0), src);
}

void TSIRBuilder::visitReturn(TSNode n, const std::string& src) {
    if (ts_node_named_child_count(n) > 0 && currentFunc && currentRetVal) {
        NodeID v = visitExpr(ts_node_named_child(n, 0), src);
        // Only wire the return flow for pointer-returning functions. For
        // int-returning functions (the common `return 0;` case) there is
        // no pointer value to propagate, and emitting a Copy would violate
        // the verifier's pointer-typed invariant.
        if (currentRetIsPtr && pag->hasGNode(v) && pag->getGNode(v)->isPointer())
            B.addCopyStmt(v, currentRetVal);
    }
}

void TSIRBuilder::visitIf(TSNode n, const std::string& src) {
    TSNode cond = ts_node_child_by_field_name(n, "condition", 9);
    TSNode cons = ts_node_child_by_field_name(n, "consequence", 11);
    TSNode alt  = ts_node_child_by_field_name(n, "alternative", 11);
    if (!ts_node_is_null(cond)) visitExpr(cond, src);
    if (!ts_node_is_null(cons)) visitStmt(cons, src);
    if (!ts_node_is_null(alt))  visitStmt(alt, src);
}

void TSIRBuilder::visitWhile(TSNode n, const std::string& src) {
    TSNode cond = ts_node_child_by_field_name(n, "condition", 9);
    TSNode body = ts_node_child_by_field_name(n, "body", 4);
    if (!ts_node_is_null(cond)) visitExpr(cond, src);
    if (!ts_node_is_null(body)) visitStmt(body, src);
}

void TSIRBuilder::visitFor(TSNode n, const std::string& src) {
    uint32_t cn = ts_node_named_child_count(n);
    for (uint32_t i = 0; i < cn; ++i) {
        TSNode c = ts_node_named_child(n, i);
        const char* k = ts_node_type(c);
        if (strcmp(k, "compound_statement") == 0) visitStmt(c, src);
        else if (strcmp(k, "declaration") == 0)    visitLocalDecl(c, src);
        else                                       visitExpr(c, src);
    }
}

// ---------------- expressions ----------------

NodeID TSIRBuilder::visitExpr(TSNode n, const std::string& src) {
    if (ts_node_is_null(n)) return freshValVar(nullptr);
    const char* k = ts_node_type(n);
    if (strcmp(k, "identifier") == 0)              return visitIdentifier(n, src);
    if (strcmp(k, "number_literal") == 0)          return visitNumber(n, src);
    if (strcmp(k, "string_literal") == 0)          return visitString(n, src);
    if (strcmp(k, "char_literal") == 0)            return visitNumber(n, src);
    if (strcmp(k, "null") == 0)                    return freshValVar(SVFType::getSVFPtrType());
    if (strcmp(k, "unary_expression") == 0 ||
        strcmp(k, "pointer_expression") == 0 ||
        strcmp(k, "address_of_expression") == 0)   return visitUnary(n, src);
    if (strcmp(k, "binary_expression") == 0)       return visitBinary(n, src);
    if (strcmp(k, "assignment_expression") == 0)   return visitAssign(n, src);
    if (strcmp(k, "field_expression") == 0)        return visitField(n, src);
    if (strcmp(k, "subscript_expression") == 0)    return visitSubscript(n, src);
    if (strcmp(k, "call_expression") == 0)         return visitCall(n, src);
    if (strcmp(k, "cast_expression") == 0)         return visitCast(n, src);
    if (strcmp(k, "parenthesized_expression") == 0 && ts_node_named_child_count(n) > 0)
        return visitExpr(ts_node_named_child(n, 0), src);
    // generic descent: take last child
    if (ts_node_named_child_count(n) > 0)
        return visitExpr(ts_node_named_child(n, ts_node_named_child_count(n) - 1), src);
    return freshValVar(nullptr);
}

NodeID TSIRBuilder::visitIdentifier(TSNode n, const std::string& src) {
    std::string name = text(n, src);
    const Symbol* sym = st->lookup(name);
    if (!sym) {
        // unknown identifier — create dummy
        return freshValVar(nullptr);
    }
    // Functions used by name yield their address ValVar directly.
    if (sym->isFunction) return sym->valId;
    // Arrays decay to their address — no load. The identifier's value is
    // the pointer-to-first-element, which in our PAG model is the ValVar
    // that already holds `&arr.obj`.
    if (sym->isArray) return sym->valId;
    // Otherwise it's a stack/global slot: the identifier's value is what
    // the slot currently holds, i.e. a load through its address.
    NodeID dst = freshValVar(SVFType::getSVFPtrType());
    B.addLoadStmt(sym->valId, dst);
    // Propagate struct tagging across the load so chained accesses like
    // `p->next->data` (where `p` is `struct Node *`) continue to resolve
    // constant field offsets.
    tagStruct(dst, lookupNodeStruct(sym->valId));
    return dst;
}

NodeID TSIRBuilder::visitNumber(TSNode /*n*/, const std::string& /*src*/) {
    return freshValVar(SVFType::getSVFInt8Type());
}

NodeID TSIRBuilder::visitString(TSNode /*n*/, const std::string& /*src*/) {
    NodeID obj = NodeIDAllocator::get()->allocateObjectId();
    auto* info = new ObjTypeInfo(SVFType::getSVFInt8Type(), 1);
    info->setFlag(ObjTypeInfo::CONST_DATA);
    info->setByteSizeOfObj(1);
    B.addConstantAggObjNode(obj, info, currentICFG);
    NodeID v = freshValVar(SVFType::getSVFPtrType());
    B.addAddrStmt(obj, v);
    return v;
}

NodeID TSIRBuilder::visitUnary(TSNode n, const std::string& src) {
    // operator field
    TSNode op = ts_node_child_by_field_name(n, "operator", 8);
    TSNode arg = ts_node_child_by_field_name(n, "argument", 8);
    if (ts_node_is_null(arg) && ts_node_named_child_count(n) > 0)
        arg = ts_node_named_child(n, 0);
    std::string opS = ts_node_is_null(op) ? std::string() : text(op, src);
    if (opS == "&") {
        // &x : return the ValVar of x (which already holds &x_mem via AddrStmt)
        return lvalueAddress(arg, src);
    }
    if (opS == "*") {
        NodeID p = visitExpr(arg, src);
        NodeID dst = freshValVar(SVFType::getSVFPtrType());
        B.addLoadStmt(p, dst);
        return dst;
    }
    return visitExpr(arg, src);
}

NodeID TSIRBuilder::visitBinary(TSNode n, const std::string& src) {
    TSNode l = ts_node_child_by_field_name(n, "left", 4);
    TSNode r = ts_node_child_by_field_name(n, "right", 5);
    TSNode opN = ts_node_child_by_field_name(n, "operator", 8);
    NodeID lv = ts_node_is_null(l) ? 0 : visitExpr(l, src);
    NodeID rv = ts_node_is_null(r) ? 0 : visitExpr(r, src);
    std::string op = ts_node_is_null(opN) ? "" : text(opN, src);

    // Pointer arithmetic: `p + n`, `p - n`, `n + p` collapse to `&p[0]`
    // under SVF's array-insensitive model (Cat 4). We need to recognise a
    // "C pointer" *source-level*, because in our PAG model every local
    // ValVar is nominally pointer-typed (it holds an alloca address), so
    // `isPointer()` is too eager and would fire on `i + 1`.
    auto isCPointerExpr = [&](TSNode x) -> bool {
        if (ts_node_is_null(x)) return false;
        const char* k = ts_node_type(x);
        if (strcmp(k, "identifier") == 0) {
            const Symbol* sym = st->lookup(text(x, src));
            return sym && sym->type == tb->getPtrType();
        }
        return strcmp(k, "field_expression")     == 0 ||
               strcmp(k, "subscript_expression") == 0 ||
               strcmp(k, "cast_expression")      == 0 ||
               strcmp(k, "pointer_expression")   == 0;
    };
    if (op == "+" || op == "-") {
        bool lp = isCPointerExpr(l);
        bool rp = isCPointerExpr(r);
        if (lp || rp) {
            NodeID base = lp ? lv : rv;
            NodeID g = gep->addConstFieldGep(base, 0, SVFType::getSVFPtrType(),
                                             currentICFG);
            tagStruct(g, lookupNodeStruct(base));
            return g;
        }
    }
    return freshValVar(nullptr);
}

NodeID TSIRBuilder::visitAssign(TSNode n, const std::string& src) {
    TSNode l = ts_node_child_by_field_name(n, "left", 4);
    TSNode r = ts_node_child_by_field_name(n, "right", 5);
    NodeID rhs = visitExpr(r, src);
    // l-value handling
    const char* k = ts_node_type(l);
    if (strcmp(k, "identifier") == 0) {
        const Symbol* sym = st->lookup(text(l, src));
        if (sym) B.addStoreStmt(rhs, sym->valId, currentICFG);
        return rhs;
    }
    if (strcmp(k, "pointer_expression") == 0 || strcmp(k, "unary_expression") == 0) {
        // *p = rhs
        TSNode arg = ts_node_child_by_field_name(l, "argument", 8);
        if (ts_node_is_null(arg) && ts_node_named_child_count(l) > 0)
            arg = ts_node_named_child(l, 0);
        NodeID p = visitExpr(arg, src);
        B.addStoreStmt(rhs, p, currentICFG);
        return rhs;
    }
    if (strcmp(k, "field_expression") == 0 ||
        strcmp(k, "subscript_expression") == 0) {
        // `s.f = rhs`, `p->f = rhs`, `arr[i] = rhs`, `s.arr[i].f = rhs`
        // all share the same shape: compute the lvalue address (possibly
        // a chain of const-offset GEPs) and store through it.
        NodeID dst = lvalueAddress(l, src);
        B.addStoreStmt(rhs, dst, currentICFG);
        return rhs;
    }
    return rhs;
}

NodeID TSIRBuilder::visitField(TSNode n, const std::string& src) {
    // Inline the lvalueAddress(field_expression) logic so we know the
    // resolved FieldMeta at Load time (without re-visiting sub-trees).
    TSNode obj = ts_node_child_by_field_name(n, "argument", 8);
    TSNode opN = ts_node_child_by_field_name(n, "operator", 8);
    TSNode fld = ts_node_child_by_field_name(n, "field", 5);
    std::string op    = ts_node_is_null(opN) ? "." : text(opN, src);
    std::string fname = ts_node_is_null(fld) ? "" : text(fld, src);

    NodeID baseId = (op == ".") ? lvalueAddress(obj, src)
                                : visitExpr(obj, src);
    const SVFStructType* sty = lookupNodeStruct(baseId);

    NodeID g;
    const FieldMeta* fm = nullptr;
    if (sty) {
        int idx = findFieldMetaIdx(sty, fname);
        if (idx >= 0) {
            fm = &structFields[sty][idx];
            g = gep->addConstFieldGep(baseId, (unsigned)idx, sty, currentICFG);
        } else {
            g = gep->addVariantGep(baseId, SVFType::getSVFPtrType(), currentICFG);
        }
    } else {
        g = gep->addVariantGep(baseId, SVFType::getSVFPtrType(), currentICFG);
    }
    if (fm && fm->pointee && !fm->isPointer) tagStruct(g, fm->pointee);

    NodeID dst = freshValVar(SVFType::getSVFPtrType());
    B.addLoadStmt(g, dst);
    // Pointer-to-struct field: loaded value is a struct-X lvalue.
    if (fm && fm->pointee && fm->isPointer) tagStruct(dst, fm->pointee);
    return dst;
}

NodeID TSIRBuilder::visitSubscript(TSNode n, const std::string& src) {
    NodeID g = lvalueAddress(n, src);
    NodeID dst = freshValVar(SVFType::getSVFPtrType());
    B.addLoadStmt(g, dst);
    // Array-of-struct element: propagate the struct tag across the load
    // so that `arr[i].field` can GEP into it.
    tagStruct(dst, lookupNodeStruct(g));
    return dst;
}

NodeID TSIRBuilder::visitCall(TSNode n, const std::string& src) {
    TSNode fn = ts_node_child_by_field_name(n, "function", 8);
    TSNode args = ts_node_child_by_field_name(n, "arguments", 9);
    std::string fname = ts_node_is_null(fn) ? "" : text(fn, src);
    // malloc/calloc -> heap object
    if (fname == "malloc" || fname == "calloc" || fname == "realloc") {
        NodeID heap = freshObjVar(tb->getPtrType(), ObjTypeInfo::HEAP_OBJ);
        NodeID v = freshValVar(SVFType::getSVFPtrType());
        B.addAddrStmt(heap, v);
        return v;
    }

    // Collect evaluated argument ValVars first (side-effects always happen).
    std::vector<NodeID> argVals;
    if (!ts_node_is_null(args)) {
        uint32_t an = ts_node_named_child_count(args);
        for (uint32_t i = 0; i < an; ++i)
            argVals.push_back(visitExpr(ts_node_named_child(args, i), src));
    }

    // Direct call: look up by name in the pre-scanned function table.
    auto it = funcInfos.find(fname);
    if (it == funcInfos.end()) {
        // Unknown / external / indirect — sound stub.
        return freshValVar(SVFType::getSVFPtrType());
    }
    FuncInfo& fi = it->second;

    // ICFG: insert a CallICFGNode + RetICFGNode chain between currentICFG
    // and the callee's entry/exit nodes. Subsequent intra statements of the
    // caller continue to extend from the existing currentICFG (the call/ret
    // pair hangs off as an interprocedural side-branch).
    CallICFGNode* callNode = icfgB->addCall(currentBB, fi.type, fi.fun, false);
    RetICFGNode*  retNode  = icfgB->addRet(callNode);
    if (currentICFG) icfgB->seq(currentICFG, callNode);
    icfgB->callEdge(callNode, fi.entry);
    icfgB->retEdge(fi.exit,  retNode);
    // Also wire the direct CallGraph edge so Andersen sees the callee.
    const_cast<CallGraph*>(pag->getCallGraph())
        ->addDirectCallGraphEdge(callNode, currentFunc, fi.fun);

    // CallPE for each actual→formal pair. Extra actuals are dropped (vararg
    // is not yet supported); missing actuals leave formals unbound (sound).
    size_t npairs = std::min(argVals.size(), fi.formals.size());
    for (size_t i = 0; i < npairs; ++i) {
        B.addCallPE(argVals[i], fi.formals[i], callNode, fi.entry);
    }

    // RetPE from callee return ValVar into a fresh caller-side result ValVar.
    NodeID result = freshValVar(SVFType::getSVFPtrType());
    B.addRetPE(fi.retVal, result, callNode, fi.exit);
    return result;
}

NodeID TSIRBuilder::visitCast(TSNode n, const std::string& src) {
    // A C cast is modelled as a pointer-to-pointer CopyStmt (Cat 5).
    // tree-sitter-c emits `cast_expression` with a type field and a value
    // field; the value is the last named child in practice.
    if (ts_node_named_child_count(n) == 0) return freshValVar(nullptr);
    TSNode valN = ts_node_child_by_field_name(n, "value", 5);
    if (ts_node_is_null(valN))
        valN = ts_node_named_child(n, ts_node_named_child_count(n) - 1);
    NodeID inner = visitExpr(valN, src);

    // If we're casting to a pointer type like `(struct X*)p`, propagate
    // the target struct so subsequent `->field` accesses resolve.
    const SVFStructType* castStruct = nullptr;
    TSNode typeN = ts_node_child_by_field_name(n, "type", 4);
    if (!ts_node_is_null(typeN)) {
        SVFType* castTy = tb->parseTypeFromNode(typeN, src);
        castStruct = SVFUtil::dyn_cast<SVFStructType>(castTy);
    }

    // Emit an actual CopyStmt so the cast survives as a dedicated edge.
    // Only legal when the source is pointer-typed; otherwise pass-through.
    if (pag->hasGNode(inner) && pag->getGNode(inner)->isPointer()) {
        NodeID dst = freshValVar(SVFType::getSVFPtrType());
        B.addCopyStmt(inner, dst);
        if (castStruct) tagStruct(dst, castStruct);
        else            tagStruct(dst, lookupNodeStruct(inner));
        return dst;
    }
    return inner;
}

// -------- GEP core --------
//
// lvalueAddress returns a NodeID holding the ADDRESS of the given lvalue
// expression (no final load). It is the canonical place where GepStmts are
// emitted; visitField/visitSubscript are thin wrappers that add a Load on
// top when the outer context wants an rvalue. The recursive structure lets
// chained accesses (`s.a.b`, `p->next->data`, `arr[i].field`) propagate
// the struct-type chain through `nodeStructTy` and produce a constant-
// offset GepStmt at every link whenever the type is known.
NodeID TSIRBuilder::lvalueAddress(TSNode n, const std::string& src) {
    if (ts_node_is_null(n)) return freshValVar(nullptr);
    const char* k = ts_node_type(n);

    if (strcmp(k, "parenthesized_expression") == 0 &&
        ts_node_named_child_count(n) > 0)
        return lvalueAddress(ts_node_named_child(n, 0), src);

    if (strcmp(k, "identifier") == 0) {
        const Symbol* sym = st->lookup(text(n, src));
        if (!sym) return freshValVar(nullptr);
        return sym->valId; // already holds the address
    }

    // `*p` as an lvalue: the address IS the pointer value p.
    if (strcmp(k, "pointer_expression") == 0 ||
        strcmp(k, "unary_expression") == 0) {
        TSNode opN = ts_node_child_by_field_name(n, "operator", 8);
        std::string op = ts_node_is_null(opN) ? "" : text(opN, src);
        if (op == "*") {
            TSNode arg = ts_node_child_by_field_name(n, "argument", 8);
            if (ts_node_is_null(arg) && ts_node_named_child_count(n) > 0)
                arg = ts_node_named_child(n, 0);
            NodeID p = visitExpr(arg, src);
            return p;
        }
    }

    if (strcmp(k, "field_expression") == 0) {
        TSNode obj = ts_node_child_by_field_name(n, "argument", 8);
        TSNode opN = ts_node_child_by_field_name(n, "operator", 8);
        TSNode fld = ts_node_child_by_field_name(n, "field", 5);
        std::string op    = ts_node_is_null(opN) ? "." : text(opN, src);
        std::string fname = ts_node_is_null(fld) ? "" : text(fld, src);

        // For `.` the base is the address of the containing struct (an
        // lvalue); for `->` the base is the pointer value that points to
        // the containing struct (an rvalue).
        NodeID baseId = (op == ".") ? lvalueAddress(obj, src)
                                    : visitExpr(obj, src);
        const SVFStructType* sty = lookupNodeStruct(baseId);

        NodeID g;
        const FieldMeta* fm = nullptr;
        if (sty) {
            int idx = findFieldMetaIdx(sty, fname);
            if (idx >= 0) {
                fm = &structFields[sty][idx];
                g = gep->addConstFieldGep(baseId, (unsigned)idx, sty, currentICFG);
            } else {
                g = gep->addVariantGep(baseId, SVFType::getSVFPtrType(), currentICFG);
            }
        } else {
            g = gep->addVariantGep(baseId, SVFType::getSVFPtrType(), currentICFG);
        }

        // Propagate struct type through the GEP result for chaining.
        //   - Direct nested struct field (`struct Inner inner;`): g is the
        //     address of the embedded sub-object, which IS a struct Inner
        //     lvalue — tag it.
        //   - Pointer-to-struct field (`struct Inner *p;`): g is the
        //     address of the slot holding the pointer; dereferencing it
        //     yields a struct Inner lvalue, so tagging happens in the
        //     enclosing visitField's load destination, not here.
        if (fm && fm->pointee && !fm->isPointer)
            tagStruct(g, fm->pointee);
        return g;
    }

    if (strcmp(k, "subscript_expression") == 0) {
        TSNode arr = ts_node_child_by_field_name(n, "argument", 8);
        TSNode idx = ts_node_child_by_field_name(n, "index", 5);
        // The base of an array access is itself an lvalue: `s.arr[i]`
        // wants the address of `s.arr`, not a load of it. When the base
        // is an identifier though, decay semantics dictate using the
        // loaded pointer (so `int *p; p[i]` works).
        NodeID baseId;
        const char* bk = ts_node_type(arr);
        if (strcmp(bk, "identifier") == 0)
            baseId = visitExpr(arr, src); // array decay
        else
            baseId = lvalueAddress(arr, src);
        if (!ts_node_is_null(idx)) visitExpr(idx, src); // side-effects
        // SVF is array-insensitive: every element collapses to field 0.
        const SVFStructType* baseSty = lookupNodeStruct(baseId);
        NodeID g = gep->addConstFieldGep(baseId, 0, SVFType::getSVFPtrType(),
                                         currentICFG);
        // arrays-of-structs preserve the element struct type.
        if (baseSty) tagStruct(g, baseSty);
        return g;
    }

    // Anything else: evaluate as rvalue and use the resulting node as
    // the "address" — this is wrong for non-pointer scalars but lets the
    // visitor degrade gracefully.
    return visitExpr(n, src);
}

} // namespace svfts
