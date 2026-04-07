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

    /// Get text of a tree-sitter node.
    std::string text(TSNode n, const std::string& src) const {
        return src.substr(ts_node_start_byte(n),
                          ts_node_end_byte(n) - ts_node_start_byte(n));
    }

    /// Find the field-index of `fieldName` in struct `st`. -1 if not found.
    int findFieldIdx(const SVF::SVFStructType* st, const std::string& fieldName) const;

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

    // Track field names per struct (since SVFStructType doesn't carry them).
    std::unordered_map<const SVF::SVFStructType*, std::vector<std::string>> structFieldNames;
};

} // namespace svfts
