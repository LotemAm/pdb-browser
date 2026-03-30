#pragma once
#include "PdbIndex.h"

#include <atlbase.h>  // CComPtr, CComBSTR
#include <dia2.h>

#include <expected>
#include <memory>
#include <string>

// Wraps the DIA SDK lifecycle for loading a single PDB.
// Lives entirely on the background thread — never touched by the main thread.
class PdbSession {
public:
    PdbSession();
    ~PdbSession();

    // Loads the PDB at `path`, builds a PdbIndex, and returns it.
    std::expected<std::shared_ptr<PdbIndex>, std::string> load(const std::string& path);

    // Loads nested data of a symbol
    std::expected<SymbolNode, std::string> loadSymbolData(std::shared_ptr<PdbIndex> activeIndex, SymbolId id);

    // Resolve compilandId for all symbols that don't have one yet.
    // Called from a background thread; writes directly to SymbolNode::compilandId.
    void resolveAllCompilandIds(PdbIndex& index, std::atomic<int>& progress, std::atomic<bool>& cancel);

private:
    void indexCompilands(struct IDiaSession* session, PdbIndex& index);
    void indexTypes(struct IDiaSession* session, PdbIndex& index);
    void indexGlobals(struct IDiaSession* session, PdbIndex& index);

    // Convert a single IDiaSymbol to a SymbolNode (no children yet).
    SymbolNode extractSymbol(struct IDiaSymbol* sym, SymbolKind kind);

    void getCompilandSymbolData(SymbolId targetId, IDiaSymbol* diaSym, PdbIndex& index);
    void getFunctionSymbolData(SymbolId targetId, IDiaSymbol* diaSym, PdbIndex& index);
    void getUDTSymbolData(SymbolId targetId, IDiaSymbol* diaSym, PdbIndex& index);
    void getEnumSymbolData(SymbolId targetId, IDiaSymbol* diaSym, PdbIndex& index);
    void getTypedefSymbolData(SymbolId targetId, IDiaSymbol* diaSym, PdbIndex& index);
    void getDataSymbolData(SymbolId targetId, IDiaSymbol* diaSym, PdbIndex& index);

    // Add a type (and its pointer/array chain) to the index, returning its SymbolId.
    SymbolId addTypeChainToIndex(IDiaSymbol* typeSym, PdbIndex& index, int depth = 0);

    // Resolve the compiland a DIA symbol belongs to (multi-strategy).
    SymbolId resolveLexicalCompiland(IDiaSymbol* diaSym, PdbIndex& index);
    SymbolId registerCompiland(IDiaSymbol* compilandSym, PdbIndex& index);
    SymbolId findCompilandByRVA(DWORD rva, PdbIndex& index);

    CComPtr<IDiaDataSource> m_source;
    CComPtr<IDiaSession> m_session;
};
