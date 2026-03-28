#pragma once
#include "codegen/CodeGen.h"
#include "pdb/SymbolNode.h"

#include <atomic>
#include <string>
#include <vector>

struct AppState;

class InspectorPanel {
public:
    void render(AppState* state);

private:
    void renderCodePopup(AppState* state);
    void regenerateCode(AppState* state);

    // "View as Code" popup state
    bool                   m_showCodePopup{false};
    int                    m_selectedLang{0};
    std::vector<CodeLang>  m_applicableLangs;
    std::string            m_generatedCode;

    // Optional sections checkboxes + loading
    bool                   m_includeStatics{false};
    bool                   m_includeFunctions{false};
    std::atomic<bool>      m_childrenLoading{false};
    bool                   m_wasLoading{false};
    SymbolId               m_childrenLoadedFor{INVALID_SYMBOL_ID};
};
