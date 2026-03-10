#pragma once
#include <string>
#include <vector>

#include "pdb/SymbolNode.h"

struct AppState;

class SearchDialog {
public:
    void render(AppState* state);

private:
    void rebuildResults(const AppState* state);

    char m_queryBuf[256] = {};
    int  m_kindFilter = 0; // 0 = All

    // Cached query state — results recomputed only when these change
    std::string          m_cachedQuery;
    int                  m_cachedKindFilter = 0;
    const void*          m_cachedIndex = nullptr; // identity check for PdbIndex swap
    bool                 m_cachedPrettify = true; // track prettify toggle for invalidation
    std::vector<SymbolId> m_results;
};
