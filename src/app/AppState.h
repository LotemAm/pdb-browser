#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "pdb/PdbIndex.h"
#include "pdb/SymbolNode.h"

class App;
class PdbSession;

enum class LoadState { Idle, Loading, Ready, Failed };

enum class BrowserTab { Types, Enums, Functions, Compilands, Typedefs, Globals };

// Handoff struct populated by the background loading thread.
struct LoadResult {
    std::shared_ptr<PdbIndex> index;
    std::string errorMessage;
    SymbolId loadedSymbolId{INVALID_SYMBOL_ID};  // set after per-symbol data load
};

// Central shared state — passed by pointer to every panel.
// All writes from the background thread go through loadResult + loadState.
// The main thread swaps loadResult into activeIndex when state reaches Ready.
struct AppState {
    App* app;

    // ── PDB loading ──────────────────────────────────────────────────────────
    std::atomic<LoadState> loadState{LoadState::Idle};
    std::mutex             loadMutex;
    LoadResult             loadResult;        // written by bg thread under mutex
    std::shared_ptr<PdbIndex> activeIndex;    // main-thread while not loading
    std::unique_ptr<PdbSession> activePdbSession; // can be used outside main-thread

    // ── Selection ────────────────────────────────────────────────────────────
    SymbolId selectedSymbol{INVALID_SYMBOL_ID};

    // ── Navigation history ───────────────────────────────────────────────────
    std::vector<SymbolId> navBack;
    std::vector<SymbolId> navForward;

    // ── Search ───────────────────────────────────────────────────────────────
    std::string searchQuery;

    // ── UI flags ─────────────────────────────────────────────────────────────
    bool requestOpen{false};          // set by File > Open to trigger dialog
    std::string pendingPdbPath;       // path chosen in dialog
    bool showSearch{false};           // Ctrl+F search dialog
    bool searchFocusInput{false};     // pulse: focus search input on next render
    bool prettifyNames{true};         // toggle: simplify verbose C++/Rust names

    // ── Browser tab ──────────────────────────────────────────────────────────
    BrowserTab activeBrowserTab{BrowserTab::Types};

    // ── Export ────────────────────────────────────────────────────────────────
    bool         showExportDialog{false};                  // open the export settings dialog
    std::string  exportStatusMessage;                      // success/error feedback toast
    float        exportStatusTimer{0.f};                   // seconds remaining to show status

    // ── Source file cache ────────────────────────────────────────────────────
    std::unordered_map<std::string, bool> fileExistsCache;

    void selectSymbol(SymbolId id);
    bool canNavigateBack() const    { return !navBack.empty(); }
    bool canNavigateForward() const { return !navForward.empty(); }
    void navigateBack();
    void navigateForward();
};
