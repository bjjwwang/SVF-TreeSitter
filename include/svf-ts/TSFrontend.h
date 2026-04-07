// TSFrontend.h — main entry: source files → SVFIR
#pragma once
#include "SVFIR/SVFIR.h"
#include <tree_sitter/api.h>
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
};

} // namespace svfts
