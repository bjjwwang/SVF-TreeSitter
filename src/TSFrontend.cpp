#include "svf-ts/TSFrontend.h"
#include "svf-ts/TSTypeBuilder.h"
#include "svf-ts/TSSymbolTable.h"
#include "svf-ts/TSICFGBuilder.h"
#include "svf-ts/GepHandler.h"
#include "svf-ts/TSIRBuilder.h"
#include "Graphs/ICFG.h"
#include "svf-ts/SvfShim.h"
#include "Util/NodeIDAllocator.h"
#include <fstream>
#include <sstream>
#include <iostream>

extern "C" const TSLanguage* tree_sitter_c(void);

using namespace SVF;

namespace svfts {

TSFrontend::TSFrontend() {
    parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_c());
}

TSFrontend::~TSFrontend() {
    for (auto& f : files) if (f.tree) ts_tree_delete(f.tree);
    ts_parser_delete(parser);
}

bool TSFrontend::addSourceFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) { std::cerr << "Cannot open " << path << "\n"; return false; }
    std::stringstream ss; ss << in.rdbuf();
    SourceFile f;
    f.path = path;
    f.content = ss.str();
    f.tree = ts_parser_parse_string(parser, nullptr, f.content.c_str(),
                                    (uint32_t)f.content.size());
    if (!f.tree) return false;
    files.push_back(std::move(f));
    return true;
}

SVFIR* TSFrontend::buildSVFIR() {
    SVFIR* pag = SVFIR::getPAG();
    pag->setModuleIdentifier(files.empty() ? "ts" : files.front().path);

    // Set up subordinate machinery.
    TSTypeBuilder tb; tb.init();
    TSSymbolTable sym;
    auto* icfg = new ICFG();
    // Provide a global ICFG node so that global declarations have an
    // ICFGNode to attach SVFVars to.
    ICFGBuilder(icfg).addGlobal(new GlobalICFGNode(0));
    pag->setICFG(icfg);
    TSICFGBuilder icfgB(icfg);
    GepHandler gep(pag, &tb.dl());
    TSIRBuilder ir(pag, &tb, &sym, &icfgB, &gep);
    ir.initSpecials();

    for (auto& f : files) {
        TSNode root = ts_tree_root_node(f.tree);
        ir.visitTranslationUnit(root, f.content);
    }
    ir.finalize();
    return pag;
}

} // namespace svfts
