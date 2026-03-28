#include "SearchDialog.h"
#include "UiHelpers.h"
#include "app/AppState.h"
#include "pdb/PdbIndex.h"
#include "pdb/Prettify.h"
#include "pdb/SymbolNode.h"

#include <imgui.h>

#include <format>
#include <string>

// ── helpers ──────────────────────────────────────────────────────────────────

static SymbolKind kindFromFilter(int filter)
{
    switch (filter) {
    case 1: return SymbolKind::Function;
    case 2: return SymbolKind::UDT;
    case 3: return SymbolKind::Enum;
    case 4: return SymbolKind::Typedef;
    case 5: return SymbolKind::Data;
    default: return SymbolKind::Unknown;
    }
}

// ── SearchDialog ─────────────────────────────────────────────────────────────

void SearchDialog::rebuildResults(const AppState* state)
{
    m_results.clear();
    if (!state->activeIndex || state->searchQuery.empty()) return;

    const PdbIndex* index = state->activeIndex.get();
    SymbolKind wantKind = kindFromFilter(m_kindFilter);

    constexpr int MAX_RESULTS = 1000;
    for (size_t i = 0; i < index->symbolCount() && m_results.size() < MAX_RESULTS; ++i) {
        const SymbolNode* sym = index->getSymbol(static_cast<SymbolId>(i));
        if (!sym) continue;
        if (wantKind != SymbolKind::Unknown && sym->kind != wantKind) continue;
        // Match against raw name, undecorated name, and prettified name
        if (!caseInsensitiveContains(sym->name, state->searchQuery) &&
            !caseInsensitiveContains(sym->undecoratedName, state->searchQuery) &&
            !(state->prettifyNames &&
              caseInsensitiveContains(displayName(*sym, true), state->searchQuery)))
            continue;
        m_results.push_back(sym->id);
    }

    m_cachedQuery = state->searchQuery;
    m_cachedKindFilter = m_kindFilter;
    m_cachedIndex = index;
    m_cachedPrettify = state->prettifyNames;
}

void SearchDialog::render(AppState* state)
{
    if (!state->showSearch) return;

    // On every open: center on screen
    if (state->searchFocusInput) {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({700, 520}, ImGuiCond_Always);
    }

    if (!ImGui::Begin("Search", &state->showSearch,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // Focus the query input when the dialog is first opened
    if (state->searchFocusInput) {
        ImGui::SetKeyboardFocusHere(0);
        state->searchFocusInput = false;
    }

    // Escape closes the dialog
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        state->showSearch = false;

    if (!state->activeIndex) {
        ImGui::TextDisabled("No PDB loaded.");
        ImGui::End();
        return;
    }

    // Query input
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputText("##query", m_queryBuf, sizeof(m_queryBuf),
                         ImGuiInputTextFlags_AutoSelectAll))
        state->searchQuery = m_queryBuf;

    // Kind filter
    ImGui::SetNextItemWidth(140.f);
    ImGui::Combo("Kind", &m_kindFilter,
        "All\0Function\0UDT\0Enum\0Typedef\0Data\0\0");

    if (state->searchQuery.empty()) {
        ImGui::TextDisabled("Type to search...");
        ImGui::End();
        return;
    }

    // Rebuild results only when query, kind filter, loaded PDB, or prettify toggle changes
    const void* currentIndex = state->activeIndex.get();
    if (state->searchQuery != m_cachedQuery ||
        m_kindFilter != m_cachedKindFilter ||
        currentIndex != m_cachedIndex ||
        state->prettifyNames != m_cachedPrettify)
    {
        rebuildResults(state);
    }

    // Results table
    if (ImGui::BeginTable("Results", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Kind",   ImGuiTableColumnFlags_WidthFixed, 80.f);
        ImGui::TableSetupColumn("RVA",    ImGuiTableColumnFlags_WidthFixed, 90.f);
        ImGui::TableHeadersRow();

        const PdbIndex* index = state->activeIndex.get();
        for (SymbolId id : m_results) {
            const SymbolNode* sym = index->getSymbol(id);
            if (!sym) continue;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            auto dn = displayName(*sym, state->prettifyNames);
            renderSelectableSymbolRow(state, *sym, dn);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled(kindStr(sym->kind));
            ImGui::TableSetColumnIndex(2);
            if (sym->rva) {
                auto rvaStr = std::format("0x{:08X}", sym->rva);
                ImGui::TextUnformatted(rvaStr.c_str());
            }
        }

        constexpr int MAX_RESULTS = 1000;
        if (m_results.size() == MAX_RESULTS)
            ImGui::TextDisabled("(results capped at %d)", MAX_RESULTS);

        ImGui::EndTable();
    }

    ImGui::End();
}
