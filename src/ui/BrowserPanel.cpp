#include "BrowserPanel.h"
#include "UiHelpers.h"
#include "app/AppState.h"
#include "pdb/PdbIndex.h"
#include "pdb/Prettify.h"
#include "pdb/SymbolNode.h"

#include <imgui.h>
#include <cstring>
#include <unordered_set>

// ── helpers ──────────────────────────────────────────────────────────────────

static const char* kindLabel(SymbolKind k)
{
    switch (k) {
    case SymbolKind::Compiland:  return "[OBJ]";
    case SymbolKind::Function:   return "[FN] ";
    case SymbolKind::UDT:        return "[UDT]";
    case SymbolKind::Enum:       return "[ENUM]";
    case SymbolKind::Typedef:    return "[TD] ";
    case SymbolKind::Data:       return "[VAR]";
    default:                     return "[?]  ";
    }
}

static void renderSymbolNode(AppState* state, const SymbolNode* node)
{
    if (!node) return;

    auto label = node->name.empty()
        ? std::string_view{"<anonymous>"}
        : displayName(*node, state->prettifyNames);
    bool isLeaf = node->children.empty() && node->childrenLoaded;
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_SpanAvailWidth |
        (isLeaf ? ImGuiTreeNodeFlags_Leaf : 0) |
        (node->id == state->selectedSymbol ? ImGuiTreeNodeFlags_Selected : 0);

    bool open = ImGui::TreeNodeEx(
        (void*)(uintptr_t)node->id, flags,
        "%s %.*s", kindLabel(node->kind), static_cast<int>(label.size()), label.data());

    if (ImGui::IsItemClicked())
        state->selectSymbol(node->id);

    if (open) {
        if (state->activeIndex) {
            for (SymbolId childId : node->children) {
                const SymbolNode* child = state->activeIndex->getSymbol(childId);
                renderSymbolNode(state, child);
            }
        }
        ImGui::TreePop();
    }
}

// ── filter cache ─────────────────────────────────────────────────────────────

static void filterIds(const PdbIndex* index, const std::vector<SymbolId>& source,
                      std::vector<SymbolId>& dest, const char* filter, bool prettify,
                      const std::unordered_set<SymbolId>* hiddenCompilands = nullptr,
                      bool hideUnassigned = false)
{
    dest.clear();
    bool hasFilter = filter && filter[0] != '\0';
    bool hasCompFilter = hiddenCompilands && !hiddenCompilands->empty();
    if (!hasFilter && !hasCompFilter && !hideUnassigned) {
        dest = source;
        return;
    }
    dest.reserve(source.size() / 4); // heuristic
    for (SymbolId id : source) {
        const SymbolNode* node = index->getSymbol(id);
        if (!node) continue;
        if (node->compilandId == INVALID_SYMBOL_ID) {
            if (hideUnassigned) continue;
        } else if (hasCompFilter && hiddenCompilands->contains(node->compilandId)) {
            continue;
        }
        if (hasFilter && !symbolMatchesFilter(*node, filter, prettify))
            continue;
        dest.push_back(id);
    }
}

void BrowserPanel::rebuildFilteredIds(AppState* state)
{
    PdbIndex* index = state->activeIndex.get();
    const char* filter = m_filterBuf;
    bool prettify = state->prettifyNames;
    const auto* hidden = &state->hiddenCompilands;
    bool hideUnassigned = state->hideUnassignedCompiland;

    filterIds(index, index->udts(),       m_filteredUdts,       filter, prettify, hidden, hideUnassigned);
    filterIds(index, index->enums(),      m_filteredEnums,      filter, prettify, hidden, hideUnassigned);
    filterIds(index, index->functions(),  m_filteredFunctions,  filter, prettify, hidden, hideUnassigned);
    filterIds(index, index->compilands(), m_filteredCompilands, filter, prettify);
    filterIds(index, index->typedefs(),   m_filteredTypedefs,   filter, prettify, hidden, hideUnassigned);
    filterIds(index, index->globals(),    m_filteredGlobals,    filter, prettify, hidden, hideUnassigned);

    m_cachedFilter = filter;
    m_cachedPrettify = prettify;
    m_cachedIndex = index;
    m_cachedSymbolCount = index->symbolCount();
    m_cachedHiddenCount = state->hiddenCompilands.size();
    m_cachedHideUnassigned = hideUnassigned;
}

// ── generic tab renderer ─────────────────────────────────────────────────────

// Column descriptor for browser tabs.
struct BrowserColumn {
    const char* name;
    ImGuiTableColumnFlags flags = 0;
    float width = 0.f;
};

// Render a browser tab table over a pre-filtered range of symbol IDs using ImGuiListClipper.
// `extraColumns` is called after the name column for each row.
template <typename ExtraColumnsFn>
static void renderBrowserTab(AppState* state, PdbIndex* index,
                             const char* tableId, const BrowserColumn* columns, int columnCount,
                             const std::vector<SymbolId>& filteredIds, ExtraColumnsFn extraColumns)
{
    if (!ImGui::BeginTable(tableId, columnCount, kBrowserTableFlags))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    for (int i = 0; i < columnCount; ++i)
        ImGui::TableSetupColumn(columns[i].name, columns[i].flags, columns[i].width);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(filteredIds.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const SymbolNode* node = index->getSymbol(filteredIds[row]);
            if (!node) continue;
            auto dn = displayName(*node, state->prettifyNames);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            renderSelectableSymbolRow(state, *node, dn);

            extraColumns(node);
        }
    }

    ImGui::EndTable();
}

// ── tab renderers ────────────────────────────────────────────────────────────

static void renderTypesTab(AppState* state, PdbIndex* index, const std::vector<SymbolId>& ids)
{
    BrowserColumn cols[] = {
        {"Name"},
        {"Size (bytes)", ImGuiTableColumnFlags_WidthFixed, 100.f},
        {"Loaded",       ImGuiTableColumnFlags_WidthFixed, 55.f},
    };
    renderBrowserTab(state, index, "TypesTable", cols, 3, ids,
        [](const SymbolNode* node) {
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", node->sizeBytes);
            ImGui::TableSetColumnIndex(2);
            if (node->childrenLoaded) ImGui::TextUnformatted("*");
        });
}

static void renderEnumsTab(AppState* state, PdbIndex* index, const std::vector<SymbolId>& ids)
{
    BrowserColumn cols[] = {
        {"Name"},
        {"Loaded", ImGuiTableColumnFlags_WidthFixed, 55.f},
    };
    renderBrowserTab(state, index, "EnumsTable", cols, 2, ids,
        [](const SymbolNode* node) {
            ImGui::TableSetColumnIndex(1);
            if (node->childrenLoaded) ImGui::TextUnformatted("*");
        });
}

static void renderCompilandsTab(AppState* state, PdbIndex* index, const std::vector<SymbolId>& ids)
{
    BrowserColumn cols[] = {
        {"Name"},
        {"Loaded", ImGuiTableColumnFlags_WidthFixed, 55.f},
    };
    renderBrowserTab(state, index, "CompilandsTable", cols, 2, ids,
        [](const SymbolNode* node) {
            ImGui::TableSetColumnIndex(1);
            if (node->childrenLoaded) ImGui::TextUnformatted("*");
        });
}

static void renderTypedefsTab(AppState* state, PdbIndex* index, const std::vector<SymbolId>& ids)
{
    BrowserColumn cols[] = {
        {"Name"},
        {"Loaded", ImGuiTableColumnFlags_WidthFixed, 55.f},
    };
    renderBrowserTab(state, index, "TypedefsTable", cols, 2, ids,
        [](const SymbolNode* node) {
            ImGui::TableSetColumnIndex(1);
            if (node->childrenLoaded) ImGui::TextUnformatted("*");
        });
}

static void renderGlobalsTab(AppState* state, PdbIndex* index, const std::vector<SymbolId>& ids)
{
    BrowserColumn cols[] = {{"Name"}};
    renderBrowserTab(state, index, "GlobalsTable", cols, 1, ids,
        [](const SymbolNode*) {});
}

static void renderFunctionsTab(AppState* state, PdbIndex* index, const std::vector<SymbolId>& ids)
{
    BrowserColumn cols[] = {
        {"Name"},
        {"RVA",    ImGuiTableColumnFlags_WidthFixed, 90.f},
        {"Size",   ImGuiTableColumnFlags_WidthFixed, 80.f},
        {"Loaded", ImGuiTableColumnFlags_WidthFixed, 55.f},
    };
    renderBrowserTab(state, index, "FunctionsTable", cols, 4, ids,
        [](const SymbolNode* node) {
            ImGui::TableSetColumnIndex(1);
            if (node->rva != 0) ImGui::Text("0x%08X", node->rva);
            ImGui::TableSetColumnIndex(2);
            if (node->sizeBytes > 0) ImGui::Text("%u", node->sizeBytes);
            ImGui::TableSetColumnIndex(3);
            if (node->childrenLoaded) ImGui::TextUnformatted("*");
        });
}

// ── BrowserPanel ─────────────────────────────────────────────────────────────

void BrowserPanel::render(AppState* state)
{
    ImGui::Begin("Browser");

    if (!state->activeIndex) {
        ImGui::TextDisabled("No PDB loaded.");
        ImGui::End();
        return;
    }

    PdbIndex* index = state->activeIndex.get();

    // ── Filter input ─────────────────────────────────────────────────────────
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##BrowserFilter", "Filter symbols...", m_filterBuf, sizeof(m_filterBuf));

    // Rebuild filtered ID caches only when inputs change
    bool needsRebuild = (m_cachedFilter != m_filterBuf)
                      || (m_cachedPrettify != state->prettifyNames)
                      || (m_cachedIndex != static_cast<const void*>(index))
                      || (m_cachedSymbolCount != index->symbolCount())
                      || (m_cachedHiddenCount != state->hiddenCompilands.size())
                      || (m_cachedHideUnassigned != state->hideUnassignedCompiland);
    if (needsRebuild)
        rebuildFilteredIds(state);

    if (ImGui::BeginTabBar("BrowserTabs")) {
        if (ImGui::BeginTabItem("Types"))      { state->activeBrowserTab = BrowserTab::Types;      renderTypesTab(state, index, m_filteredUdts);           ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Enums"))      { state->activeBrowserTab = BrowserTab::Enums;      renderEnumsTab(state, index, m_filteredEnums);           ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Functions"))  { state->activeBrowserTab = BrowserTab::Functions;  renderFunctionsTab(state, index, m_filteredFunctions);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Compilands")) { state->activeBrowserTab = BrowserTab::Compilands; renderCompilandsTab(state, index, m_filteredCompilands); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Typedefs"))   { state->activeBrowserTab = BrowserTab::Typedefs;   renderTypedefsTab(state, index, m_filteredTypedefs);     ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Globals"))    { state->activeBrowserTab = BrowserTab::Globals;    renderGlobalsTab(state, index, m_filteredGlobals);       ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::End();
}
