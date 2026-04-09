// TSSymbolTable.h — name → SVFVar/NodeID mapping with scopes
#pragma once
#include "SVFIR/SVFIR.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace svfts {

struct Symbol {
    SVF::NodeID valId = 0;     // ValVar id (the "name" as value)
    SVF::NodeID objId = 0;     // ObjVar id (the underlying memory)
    const SVF::SVFType* type = nullptr;
    bool isFunction = false;
    bool isParam = false;
    bool isArray = false; // declared `T v[N]` — decays to sym.valId (no load)
};

class TSSymbolTable {
public:
    void enterScope() { scopes.emplace_back(); }
    void exitScope()  { if (!scopes.empty()) scopes.pop_back(); }

    void addGlobal(const std::string& name, const Symbol& s) { globals[name] = s; }
    void addLocal (const std::string& name, const Symbol& s) {
        if (scopes.empty()) enterScope();
        scopes.back()[name] = s;
    }
    Symbol* lookupMutable(const std::string& name) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return &f->second;
        }
        auto g = globals.find(name);
        return g == globals.end() ? nullptr : &g->second;
    }
    const Symbol* lookup(const std::string& name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return &f->second;
        }
        auto g = globals.find(name);
        return g == globals.end() ? nullptr : &g->second;
    }

    const std::unordered_map<std::string, Symbol>& globalsMap() const { return globals; }

private:
    std::unordered_map<std::string, Symbol> globals;
    std::vector<std::unordered_map<std::string, Symbol>> scopes;
};

} // namespace svfts
