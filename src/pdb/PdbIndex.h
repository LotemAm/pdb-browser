#pragma once
#include "SymbolNode.h"

#include <string>
#include <unordered_map>
#include <vector>

// Flat, read-only index over all symbols in a PDB.
// Built once on the background thread; main thread gets a shared_ptr<PdbIndex>.
class PdbIndex {
public:
    // ── Build helpers (called only from background thread) ───────────────────
    // Pre-allocate storage based on estimated total symbol count.
    void reserve(size_t estimatedSymbols);
    SymbolId addSymbol(SymbolNode node);

    // ── Queries (main thread, read-only) ─────────────────────────────────────
    // Deducing this (C++23): single definition handles both const and mutable access.
    [[nodiscard]] auto getSymbol(this auto& self, SymbolId id) -> decltype(&self.m_symbols[0]) {
        if (id >= self.m_symbols.size()) return nullptr;
        return &self.m_symbols[id];
    }

    // Top-level compiland IDs (tree roots for the Browser panel)
    [[nodiscard]] const std::vector<SymbolId>& compilands() const { return m_compilands; }

    // All UDTs / enums / typedefs for the Types tree
    [[nodiscard]] const std::vector<SymbolId>& udts()       const { return m_udts; }
    [[nodiscard]] const std::vector<SymbolId>& enums()      const { return m_enums; }
    [[nodiscard]] const std::vector<SymbolId>& typedefs()   const { return m_typedefs; }

    // All functions
    [[nodiscard]] const std::vector<SymbolId>& functions()  const { return m_functions; }

    // All globals / publics
    [[nodiscard]] const std::vector<SymbolId>& globals()    const { return m_globals; }

    // PdbId → symbol
    [[nodiscard]] SymbolId findByPdbId(PdbId pdbId) const;

    // Name → symbol (may have multiple hits for overloads)
    [[nodiscard]] std::vector<SymbolId> findByName(const std::string& name) const;

    // RVA → symbol
    [[nodiscard]] SymbolId findByRva(uint32_t rva) const;

    [[nodiscard]] size_t symbolCount() const { return m_symbols.size(); }
    const std::string& pdbPath() const { return m_pdbPath; }
    void setPdbPath(std::string path) { m_pdbPath = std::move(path); }

private:
    std::vector<SymbolNode>                      m_symbols;
    std::unordered_map<PdbId, SymbolId> m_pdbIdIndex;
    std::unordered_map<std::string, std::vector<SymbolId>> m_nameIndex;
    std::unordered_map<uint32_t, SymbolId>       m_rvaIndex;

    std::vector<SymbolId> m_compilands;
    std::vector<SymbolId> m_udts;
    std::vector<SymbolId> m_enums;
    std::vector<SymbolId> m_typedefs;
    std::vector<SymbolId> m_functions;
    std::vector<SymbolId> m_globals;

    std::string m_pdbPath;
};
