#pragma once
#include "export/Exporter.h"
#include "pdb/SymbolNode.h"

#include <atomic>
#include <string>
#include <vector>

struct AppState;
enum class ExportFormat : int;

class ExportDialog {
public:
    void render(AppState* state);

    // True while a background thread is mutating the PdbIndex (load-all).
    // Callers must NOT read from the index while this returns true.
    [[nodiscard]] bool isLoading() const { return m_loading.load(); }

private:
    void collectIds(const AppState* state);
    void startLoadAll(AppState* state);
    void showSaveDialog(AppState* state);
    void executeExport(AppState* state);

    // ── Format ───────────────────────────────────────────────────────────────
    int m_format = 0;  // 0 = JSON, 1 = CSV

    // ── Scope ────────────────────────────────────────────────────────────────
    bool m_selectedOnly = false;

    // ── Kind toggles ─────────────────────────────────────────────────────────
    bool m_exportUdts       = true;
    bool m_exportEnums      = true;
    bool m_exportFunctions  = true;
    bool m_exportCompilands = false;
    bool m_exportTypedefs   = true;
    bool m_exportGlobals    = true;

    // ── Nested data toggles ──────────────────────────────────────────────────
    bool m_includeMembers      = true;   // UDT data members
    bool m_includeBaseClasses  = true;   // UDT base classes
    bool m_includeFriends      = true;   // UDT friend declarations
    bool m_includeEnumValues   = true;   // Enum enumerator values
    bool m_includeSourceFiles  = true;   // Compiland source file list
    bool m_includeTemplateArgs = true;   // Function/UDT template arguments

    // ── Load All ─────────────────────────────────────────────────────────────
    bool m_loadAll = false;

    // ── Background loading state ─────────────────────────────────────────────
    std::atomic<bool> m_loading{false};
    std::atomic<bool> m_cancelLoading{false};
    std::atomic<int>  m_loadProgress{0};
    int               m_loadTotal{0};

    // ── Save dialog state ────────────────────────────────────────────────────
    std::string           m_pendingPath;
    std::vector<SymbolId> m_exportIds;

    // ── First-open positioning ───────────────────────────────────────────────
    bool m_needsPosition = true;
};
