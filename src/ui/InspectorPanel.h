#pragma once
#include "codegen/CodeGen.h"

#include <string>
#include <vector>

struct AppState;

class InspectorPanel {
public:
    void render(AppState* state);

private:
    void renderCodePopup(AppState* state);

    // "View as Code" popup state
    bool                   m_showCodePopup{false};
    int                    m_selectedLang{0};
    std::vector<CodeLang>  m_applicableLangs;
    std::string            m_generatedCode;
};
