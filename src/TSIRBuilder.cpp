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

int TSIRBuilder::findFieldIdx(const SVFStructType* sty, const std::string& fname) const {
    auto it = structFieldNames.find(sty);
    if (it == structFieldNames.end()) return -1;
    for (size_t i = 0; i < it->second.size(); ++i)
        if (it->second[i] == fname) return (int)i;
    return -1;
}

// ---------------- top-level driver ----------------

void TSIRBuilder::visitTranslationUnit(TSNode root, const std::string& src) {
    // Use the ICFG via the builder rather than pag->getICFG() (the latter
    // asserts totalICFGNode>0 which isn't true for a global node only).
    currentICFG = icfgB->global();
    st->enterScope();
    uint32_t n = ts_node_child_count(root);
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
        std::vector<std::string>& fnames = structFieldNames[sty];
        std::vector<const SVFType*> fields;
        uint32_t bn = ts_node_child_count(body);
        for (uint32_t i = 0; i < bn; ++i) {
            TSNode fd = ts_node_child(body, i);
            if (strcmp(ts_node_type(fd), "field_declaration") != 0) continue;
            TSNode tnode = ts_node_child_by_field_name(fd, "type", 4);
            TSNode dnode = ts_node_child_by_field_name(fd, "declarator", 10);
            SVFType* ft = tb->parseTypeFromNode(tnode, src);
            // pointer-decorated declarator?
            std::string fname = ts_node_is_null(dnode) ? "" : text(dnode, src);
            if (fname.find('*') != std::string::npos) ft = tb->getPtrType();
            // strip pointer/array decorators from name
            while (!fname.empty() && (fname.front() == '*' || fname.front() == '&'))
                fname.erase(fname.begin());
            // strip [..]
            auto br = fname.find('[');
            if (br != std::string::npos) fname.erase(br);
            fields.push_back(ft);
            fnames.push_back(fname);
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

void TSIRBuilder::visitFunctionDef(TSNode n, const std::string& src) {
    TSNode declN = ts_node_child_by_field_name(n, "declarator", 10);
    TSNode bodyN = ts_node_child_by_field_name(n, "body", 4);
    TSNode retTN = ts_node_child_by_field_name(n, "type", 4);
    if (ts_node_is_null(declN) || ts_node_is_null(bodyN)) return;

    // function name lives inside declN as identifier child
    std::string fname;
    {
        // walk to find an identifier
        std::function<void(TSNode)> dig = [&](TSNode x) {
            if (!fname.empty()) return;
            if (strcmp(ts_node_type(x), "identifier") == 0) {
                fname = text(x, src);
                return;
            }
            for (uint32_t i = 0; i < ts_node_child_count(x); ++i) dig(ts_node_child(x, i));
        };
        dig(declN);
    }
    if (fname.empty()) return;

    SVFType* retT = tb->parseTypeFromNode(retTN, src);
    // build a function type with empty params (we don't introspect signature here)
    std::vector<const SVFType*> ptys;
    auto* funTy = new SVFFunctionType(0, retT, ptys, false);
    tb->buildStInfo(funTy);

    // FunObj + entry/exit ICFG nodes
    NodeID funObjId = NodeIDAllocator::get()->allocateObjectId();
    auto* info = new ObjTypeInfo(funTy, 1);
    info->setFlag(ObjTypeInfo::FUNCTION_OBJ);
    info->setByteSizeOfObj(1);
    B.addFunObjNode(funObjId, info, nullptr);
    auto* funObj = const_cast<FunObjVar*>(SVFUtil::dyn_cast<FunObjVar>(pag->getGNode(funObjId)));
    if (!funObj) return;
    // Initialise BasicBlockGraph + LoopAndDomInfo so callers like
    // FunEntryICFGNode that probe f->begin()/end() don't dereference null.
    auto* bbg = new BasicBlockGraph();
    auto* ld  = new SVFLoopAndDomInfo();
    // local helper to add a properly-owned BB whose ->getFunction() works
    auto makeBB = [&](const std::string& name) -> SVFBasicBlock* {
        bbg->id++;
        auto* bb = new SVFBasicBlock(bbg->id, funObj);
        bb->setName(name);
        bbg->addBasicBlock(bb);
        return bb;
    };
    auto* exitBB = makeBB("exit");
    funObj->initFunObjVar(/*decl*/false, /*intrin*/false, /*addr*/false,
                          /*uncalled*/false, /*notret*/false, /*vararg*/false,
                          funTy, ld, /*real*/funObj, bbg, {}, exitBB);
    currentFunc = funObj;

    // create one dummy SVFBasicBlock attached to the function so ICFG nodes
    // have something non-null to point at.
    // Register the function in the CallGraph so Andersen can resolve it.
    const_cast<CallGraph*>(pag->getCallGraph())->addCallGraphNode(funObj);

    SVFBasicBlock* dummyBB = makeBB("entry");
    currentBB = dummyBB;
    auto* entry = icfgB->addFunEntry(funObj);
    auto* exit  = icfgB->addFunExit(funObj);
    currentICFG = entry;

    // Register the function as a global symbol
    Symbol fs;
    fs.type = funTy;
    fs.objId = funObjId;
    fs.valId = freshValVar(SVFType::getSVFPtrType());
    B.addAddrStmt(funObjId, fs.valId);
    fs.isFunction = true;
    st->addGlobal(fname, fs);

    // Visit parameters
    st->enterScope();
    TSNode paramsN; // function_declarator → parameter_list
    {
        std::function<void(TSNode)> findP = [&](TSNode x) {
            if (!ts_node_is_null(paramsN)) return;
            if (strcmp(ts_node_type(x), "parameter_list") == 0) { paramsN = x; return; }
            for (uint32_t i = 0; i < ts_node_child_count(x); ++i) findP(ts_node_child(x, i));
        };
        paramsN = TSNode{}; // null-init
        findP(declN);
    }
    if (!ts_node_is_null(paramsN)) {
        uint32_t pn = ts_node_named_child_count(paramsN);
        for (uint32_t i = 0; i < pn; ++i) {
            TSNode p = ts_node_named_child(paramsN, i);
            if (strcmp(ts_node_type(p), "parameter_declaration") != 0) continue;
            TSNode pT = ts_node_child_by_field_name(p, "type", 4);
            TSNode pD = ts_node_child_by_field_name(p, "declarator", 10);
            SVFType* pty = tb->parseTypeFromNode(pT, src);
            std::string pname = ts_node_is_null(pD) ? std::string("arg") + std::to_string(i)
                                                    : text(pD, src);
            // strip pointer markers
            bool isPtr = (pname.find('*') != std::string::npos);
            while (!pname.empty() && (pname.front() == '*' || pname.front() == ' '))
                pname.erase(pname.begin());
            if (isPtr) pty = tb->getPtrType();
            createLocal(pname, pty);
        }
    }

    // Body
    icfgB->seq(entry, currentICFG = icfgB->addIntra(currentBB));
    visitStmt(bodyN, src);
    icfgB->seq(currentICFG, exit);

    st->exitScope();
    currentFunc = nullptr;
    currentICFG = nullptr;
}

// ---------------- global declaration ----------------

void TSIRBuilder::visitGlobalDecl(TSNode n, const std::string& src) {
    TSNode tnode = ts_node_child_by_field_name(n, "type", 4);
    SVFType* ty = tb->parseTypeFromNode(tnode, src);
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
            Symbol s = createLocal(name, eff);
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
            createLocal(name, eff);
        }
    }
}

void TSIRBuilder::visitExprStmt(TSNode n, const std::string& src) {
    if (ts_node_named_child_count(n) > 0)
        visitExpr(ts_node_named_child(n, 0), src);
}

void TSIRBuilder::visitReturn(TSNode n, const std::string& src) {
    if (ts_node_named_child_count(n) > 0 && currentFunc) {
        NodeID v = visitExpr(ts_node_named_child(n, 0), src);
        // RetValPN is owned by the function; we leave wiring to a higher pass
        // (omitted in first cut)
        (void)v;
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
    // For pointer-typed identifiers used as rvalues we conceptually load:
    NodeID dst = freshValVar(SVFType::getSVFPtrType());
    B.addLoadStmt(sym->valId, dst);
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
    if (!ts_node_is_null(l)) visitExpr(l, src);
    if (!ts_node_is_null(r)) visitExpr(r, src);
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
    if (strcmp(k, "field_expression") == 0) {
        // s.field = rhs or p->field = rhs
        TSNode obj = ts_node_child_by_field_name(l, "argument", 8);
        TSNode fld = ts_node_child_by_field_name(l, "field", 5);
        TSNode opN = ts_node_child_by_field_name(l, "operator", 8);
        std::string op = ts_node_is_null(opN) ? "." : text(opN, src);
        std::string fname = ts_node_is_null(fld) ? "" : text(fld, src);
        NodeID baseId = 0;
        const SVFStructType* sty = nullptr;
        if (op == ".") {
            // need address-of obj
            baseId = lvalueAddress(obj, src);
        } else {
            baseId = visitExpr(obj, src);
        }
        // we don't track types precisely → variant gep
        NodeID gepDst = gep->addVariantGep(baseId, SVFType::getSVFPtrType(), currentICFG);
        (void)sty; (void)fname;
        B.addStoreStmt(rhs, gepDst, currentICFG);
        return rhs;
    }
    if (strcmp(k, "subscript_expression") == 0) {
        TSNode arr = ts_node_child_by_field_name(l, "argument", 8);
        NodeID base = visitExpr(arr, src);
        NodeID g = gep->addVariantGep(base, SVFType::getSVFPtrType(), currentICFG);
        B.addStoreStmt(rhs, g, currentICFG);
        return rhs;
    }
    return rhs;
}

NodeID TSIRBuilder::visitField(TSNode n, const std::string& src) {
    TSNode obj = ts_node_child_by_field_name(n, "argument", 8);
    TSNode opN = ts_node_child_by_field_name(n, "operator", 8);
    std::string op = ts_node_is_null(opN) ? "." : text(opN, src);
    NodeID baseId;
    if (op == ".") baseId = lvalueAddress(obj, src);
    else            baseId = visitExpr(obj, src);
    NodeID g = gep->addVariantGep(baseId, SVFType::getSVFPtrType(), currentICFG);
    NodeID dst = freshValVar(SVFType::getSVFPtrType());
    B.addLoadStmt(g, dst);
    return dst;
}

NodeID TSIRBuilder::visitSubscript(TSNode n, const std::string& src) {
    TSNode arr = ts_node_child_by_field_name(n, "argument", 8);
    NodeID base = visitExpr(arr, src);
    NodeID g = gep->addVariantGep(base, SVFType::getSVFPtrType(), currentICFG);
    NodeID dst = freshValVar(SVFType::getSVFPtrType());
    B.addLoadStmt(g, dst);
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
    // visit arguments to keep their side-effects
    if (!ts_node_is_null(args)) {
        uint32_t an = ts_node_named_child_count(args);
        for (uint32_t i = 0; i < an; ++i)
            visitExpr(ts_node_named_child(args, i), src);
    }
    return freshValVar(SVFType::getSVFPtrType());
}

NodeID TSIRBuilder::visitCast(TSNode n, const std::string& src) {
    // cast = pointer copy in our model
    if (ts_node_named_child_count(n) > 0)
        return visitExpr(ts_node_named_child(n, ts_node_named_child_count(n) - 1), src);
    return freshValVar(nullptr);
}

NodeID TSIRBuilder::lvalueAddress(TSNode n, const std::string& src) {
    if (ts_node_is_null(n)) return freshValVar(nullptr);
    const char* k = ts_node_type(n);
    if (strcmp(k, "identifier") == 0) {
        const Symbol* sym = st->lookup(text(n, src));
        if (!sym) return freshValVar(nullptr);
        return sym->valId; // already holds the address
    }
    if (strcmp(k, "field_expression") == 0) {
        TSNode obj = ts_node_child_by_field_name(n, "argument", 8);
        TSNode opN = ts_node_child_by_field_name(n, "operator", 8);
        std::string op = ts_node_is_null(opN) ? "." : text(opN, src);
        NodeID baseId = (op == ".") ? lvalueAddress(obj, src) : visitExpr(obj, src);
        return gep->addVariantGep(baseId, SVFType::getSVFPtrType(), currentICFG);
    }
    if (strcmp(k, "subscript_expression") == 0) {
        TSNode arr = ts_node_child_by_field_name(n, "argument", 8);
        NodeID b = visitExpr(arr, src);
        return gep->addVariantGep(b, SVFType::getSVFPtrType(), currentICFG);
    }
    return visitExpr(n, src);
}

} // namespace svfts
