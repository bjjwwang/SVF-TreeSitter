// TSFrontend.h — main entry: source files → SVFIR
#pragma once
#include "SVFIR/SVFIR.h"
#include "svf-ts/TSTypeBuilder.h"
#include "svf-ts/TSSymbolTable.h"
#include <tree_sitter/api.h>
#include <memory>
#include <string>
#include <vector>

namespace svfts {

class TSFrontend {
public:
    TSFrontend();
    ~TSFrontend();

    bool addSourceFile(const std::string& path);
    SVF::SVFIR* buildSVFIR();

private:
    struct SourceFile {
        std::string path;
        std::string content;
        TSTree* tree = nullptr;
    };
    TSParser* parser;
    std::vector<SourceFile> files;
    // These must out-live the SVFIR — SVFVar nodes hold non-owning pointers
    // into TSTypeBuilder's owned types. Don't leave them on a stack frame.
    std::unique_ptr<TSTypeBuilder> tb;
    std::unique_ptr<TSSymbolTable> sym;
};

} // namespace svfts
