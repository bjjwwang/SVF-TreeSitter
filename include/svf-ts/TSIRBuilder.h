// TSIRBuilder.h — Visit tree-sitter-c CST and emit SVFIR
#pragma once
#include "SVFIR/SVFIR.h"
#include "svf-ts/TSTypeBuilder.h"
#include "svf-ts/TSSymbolTable.h"
#include "svf-ts/TSICFGBuilder.h"
#include "svf-ts/GepHandler.h"
#include <tree_sitter/api.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace svfts {

class TSIRBuilder {
public:
    TSIRBuilder(SVF::SVFIR* pag, TSTypeBuilder* tb, TSSymbolTable* st,
                TSICFGBuilder* icfg, GepHandler* gep);

    /// Initialize: create BlackHole, NullPtr, etc.
    void initSpecials();

    /// Visit a translation_unit (top-level CST root) for a single file.
    void visitTranslationUnit(TSNode root, const std::string& src);

    /// Connect global init → main entry, finalize.
    void finalize();

private:
    // -------- top-level dispatch --------
    void visitTopLevelDecl(TSNode n, const std::string& src);
    void visitFunctionDef(TSNode n, const std::string& src);
    void visitGlobalDecl(TSNode n, const std::string& src);

    /// Pre-pass: register every top-level function (signature, FunObjVar,
    /// entry/exit ICFG nodes, formal ValVars, ret ValVar) BEFORE visiting
    /// any function body. This lets visitCall resolve forward references
    /// and emit CallPE/RetPE without a second pass.
    void prescanFunctionDef(TSNode n, const std::string& src);

    // -------- statements --------
    void visitStmt(TSNode n, const std::string& src);
    void visitCompound(TSNode n, const std::string& src);
    void visitLocalDecl(TSNode n, const std::string& src);
    void visitExprStmt(TSNode n, const std::string& src);
    void visitReturn(TSNode n, const std::string& src);
    void visitIf(TSNode n, const std::string& src);
    void visitWhile(TSNode n, const std::string& src);
    void visitFor(TSNode n, const std::string& src);

    // -------- expressions: returns the ValVar NodeID holding the value --------
    SVF::NodeID visitExpr(TSNode n, const std::string& src);
    SVF::NodeID visitIdentifier(TSNode n, const std::string& src);
    SVF::NodeID visitNumber(TSNode n, const std::string& src);
    SVF::NodeID visitString(TSNode n, const std::string& src);
    SVF::NodeID visitUnary(TSNode n, const std::string& src);
    SVF::NodeID visitBinary(TSNode n, const std::string& src);
    SVF::NodeID visitAssign(TSNode n, const std::string& src);
    SVF::NodeID visitField(TSNode n, const std::string& src);
    SVF::NodeID visitSubscript(TSNode n, const std::string& src);
    SVF::NodeID visitCall(TSNode n, const std::string& src);
    SVF::NodeID visitCast(TSNode n, const std::string& src);

    // -------- helpers --------
    /// Get the ValVar (a "loaded" pointer-typed value) for an lvalue expression.
    /// For `&x` style we want the address; this returns a node *holding* the
    /// address (i.e. the ValVar of x). For `*p` we'd return a freshly loaded one.
    SVF::NodeID lvalueAddress(TSNode n, const std::string& src);

    /// Allocate a fresh ValVar.
    SVF::NodeID freshValVar(const SVF::SVFType* ty);
    /// Allocate a fresh ObjVar (stack/heap/global)
    SVF::NodeID freshObjVar(const SVF::SVFType* ty, unsigned flag);
    /// Create a value/obj pair for a local variable, attach AddrStmt.
    Symbol createLocal(const std::string& name, const SVF::SVFType* ty);
    Symbol createGlobal(const std::string& name, const SVF::SVFType* ty);
    /// set name on a node id (idempotent, ignored if not present)
    void nameNode(SVF::NodeID id, const std::string& name);

    /// Get text of a tree-sitter node.
    std::string text(TSNode n, const std::string& src) const {
        return src.substr(ts_node_start_byte(n),
                          ts_node_end_byte(n) - ts_node_start_byte(n));
    }


private:
    SVF::SVFIR* pag;
    TSTypeBuilder* tb;
    TSSymbolTable* st;
    TSICFGBuilder* icfgB;
    GepHandler* gep;

    // current function context
    const SVF::FunObjVar* currentFunc = nullptr;
    SVF::ICFGNode* currentICFG = nullptr;
    SVF::SVFBasicBlock* currentBB = nullptr;
    SVF::NodeID currentRetVal = 0;
    bool currentRetIsPtr = false;

    // Per-function metadata, populated by prescanFunctionDef and consumed
    // by visitFunctionDef + visitCall. Keyed by function name (source-level).
    struct FuncInfo {
        SVF::FunObjVar* fun = nullptr;
        SVF::FunEntryICFGNode* entry = nullptr;
        SVF::FunExitICFGNode* exit  = nullptr;
        SVF::NodeID funObjId = 0;
        SVF::NodeID retVal   = 0;          // callee-owned return ValVar
        std::vector<SVF::NodeID> formals;  // formal ValVars (one per param)
        std::vector<std::string> formalNames;
        std::vector<const SVF::SVFStructType*> formalStructTys; // null if not a struct
        SVF::SVFBasicBlock* entryBB = nullptr;
        SVF::SVFBasicBlock* exitBB  = nullptr;
        SVF::SVFFunctionType* type = nullptr;
        SVF::BasicBlockGraph* bbg = nullptr;
        bool defined = false;
        bool retIsPtr = false;
    };
    std::unordered_map<std::string, FuncInfo> funcInfos;

    // Per-field metadata for structs, stored in declared order. For each
    // field we track its source name and (if the field is itself a struct
    // or a pointer-to-struct) the struct type it ultimately refers to —
    // enough to resolve chained field accesses.
    struct FieldMeta {
        std::string name;
        const SVF::SVFStructType* pointee = nullptr; // struct X (for X or X*)
        bool isPointer = false;                      // field is a pointer
    };
    std::unordered_map<const SVF::SVFStructType*, std::vector<FieldMeta>> structFields;

    // Unified "this NodeID represents a struct-X lvalue/pointer" map.
    //   * Local/global/param symbols whose declared type is `struct X`
    //     or `struct X*` get `sym.valId` tagged at creation.
    //   * Loads of such symbols propagate the tag to the load dst.
    //   * Field / subscript GEPs propagate based on FieldMeta.
    //   * Casts propagate through the CopyStmt.
    // Lookup in lvalueAddress/visitField/visitSubscript lets them emit
    // constant-offset GepStmts for chained access (s.a.b, p->next->data,
    // arr[i].field, etc.) instead of collapsing to variant GEPs.
    std::unordered_map<SVF::NodeID, const SVF::SVFStructType*> nodeStructTy;

    // Helper: tag nodeStructTy[id] = sty (no-op on nullptrs).
    void tagStruct(SVF::NodeID id, const SVF::SVFStructType* sty) {
        if (id && sty) nodeStructTy[id] = sty;
    }
    const SVF::SVFStructType* lookupNodeStruct(SVF::NodeID id) const {
        auto it = nodeStructTy.find(id);
        return it == nodeStructTy.end() ? nullptr : it->second;
    }
    // Find field metadata by source-level name.
    int findFieldMetaIdx(const SVF::SVFStructType* sty,
                         const std::string& fname) const;
};

} // namespace svfts
