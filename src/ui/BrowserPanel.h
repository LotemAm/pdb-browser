#pragma once
#include "pdb/SymbolNode.h"

#include <string>
#include <vector>

struct AppState;

class BrowserPanel {
public:
    void render(AppState* state);

private:
    void rebuildFilteredIds(AppState* state);

    char m_filterBuf[256] = "";

    // Cached filter state — filtered ID lists recomputed only when these change
    std::string m_cachedFilter;
    bool        m_cachedPrettify = true;
    const void* m_cachedIndex = nullptr;
    size_t      m_cachedSymbolCount = 0;

    // Per-tab filtered ID caches
    std::vector<SymbolId> m_filteredUdts;
    std::vector<SymbolId> m_filteredEnums;
    std::vector<SymbolId> m_filteredFunctions;
    std::vector<SymbolId> m_filteredCompilands;
    std::vector<SymbolId> m_filteredTypedefs;
    std::vector<SymbolId> m_filteredGlobals;
};
