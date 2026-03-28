#include "BrowserPanel.h"
#include "UiHelpers.h"
#include "app/AppState.h"
#include "pdb/PdbIndex.h"
#include "pdb/Prettify.h"
#include "pdb/SymbolNode.h"

#include <imgui.h>
#include <cstring>

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

// ── generic tab renderer ─────────────────────────────────────────────────────

// Column descriptor for browser tabs.
struct BrowserColumn {
    const char* name;
    ImGuiTableColumnFlags flags = 0;
    float width = 0.f;
};

// Render a browser tab table over a range of symbol IDs.
// `extraColumns` is called after the name column for each row.
template <typename IdRange, typename ExtraColumnsFn>
static void renderBrowserTab(AppState* state, PdbIndex* index, const char* filter,
                             const char* tableId, const BrowserColumn* columns, int columnCount,
                             const IdRange& ids, ExtraColumnsFn extraColumns)
{
    if (!ImGui::BeginTable(tableId, columnCount, kBrowserTableFlags))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    for (int i = 0; i < columnCount; ++i)
        ImGui::TableSetupColumn(columns[i].name, columns[i].flags, columns[i].width);
    ImGui::TableHeadersRow();

    for (SymbolId id : ids) {
        const SymbolNode* node = index->getSymbol(id);
        if (!node) continue;
        if (!symbolMatchesFilter(*node, filter, state->prettifyNames)) continue;
        auto dn = displayName(*node, state->prettifyNames);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        renderSelectableSymbolRow(state, *node, dn);

        extraColumns(node);
    }

    ImGui::EndTable();
}

// ── tab renderers ────────────────────────────────────────────────────────────

static void renderTypesTab(AppState* state, PdbIndex* index, const char* filter)
{
    BrowserColumn cols[] = {
        {"Name"},
        {"Size (bytes)", ImGuiTableColumnFlags_WidthFixed, 100.f},
        {"Loaded",       ImGuiTableColumnFlags_WidthFixed, 55.f},
    };
    renderBrowserTab(state, index, filter, "TypesTable", cols, 3, index->udts(),
        [](const SymbolNode* node) {
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", node->sizeBytes);
            ImGui::TableSetColumnIndex(2);
            if (node->childrenLoaded) ImGui::TextUnformatted("*");
        });
}

static void renderEnumsTab(AppState* state, PdbIndex* index, const char* filter)
{
    BrowserColumn cols[] = {
        {"Name"},
        {"Loaded", ImGuiTableColumnFlags_WidthFixed, 55.f},
    };
    renderBrowserTab(state, index, filter, "EnumsTable", cols, 2, index->enums(),
        [](const SymbolNode* node) {
            ImGui::TableSetColumnIndex(1);
            if (node->childrenLoaded) ImGui::TextUnformatted("*");
        });
}

static void renderCompilandsTab(AppState* state, PdbIndex* index, const char* filter)
{
    BrowserColumn cols[] = {
        {"Name"},
        {"Loaded", ImGuiTableColumnFlags_WidthFixed, 55.f},
    };
    renderBrowserTab(state, index, filter, "CompilandsTable", cols, 2, index->compilands(),
        [](const SymbolNode* node) {
            ImGui::TableSetColumnIndex(1);
            if (node->childrenLoaded) ImGui::TextUnformatted("*");
        });
}

static void renderTypedefsTab(AppState* state, PdbIndex* index, const char* filter)
{
    BrowserColumn cols[] = {
        {"Name"},
        {"Loaded", ImGuiTableColumnFlags_WidthFixed, 55.f},
    };
    renderBrowserTab(state, index, filter, "TypedefsTable", cols, 2, index->typedefs(),
        [](const SymbolNode* node) {
            ImGui::TableSetColumnIndex(1);
            if (node->childrenLoaded) ImGui::TextUnformatted("*");
        });
}

static void renderGlobalsTab(AppState* state, PdbIndex* index, const char* filter)
{
    BrowserColumn cols[] = {{"Name"}};
    renderBrowserTab(state, index, filter, "GlobalsTable", cols, 1, index->globals(),
        [](const SymbolNode*) {});
}

static void renderFunctionsTab(AppState* state, PdbIndex* index, const char* filter)
{
    BrowserColumn cols[] = {
        {"Name"},
        {"RVA",    ImGuiTableColumnFlags_WidthFixed, 90.f},
        {"Size",   ImGuiTableColumnFlags_WidthFixed, 80.f},
        {"Loaded", ImGuiTableColumnFlags_WidthFixed, 55.f},
    };
    renderBrowserTab(state, index, filter, "FunctionsTable", cols, 4, index->functions(),
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
    const char* filter = m_filterBuf;

    if (ImGui::BeginTabBar("BrowserTabs")) {
        if (ImGui::BeginTabItem("Types"))      { state->activeBrowserTab = BrowserTab::Types;      renderTypesTab(state, index, filter);      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Enums"))      { state->activeBrowserTab = BrowserTab::Enums;      renderEnumsTab(state, index, filter);      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Functions"))  { state->activeBrowserTab = BrowserTab::Functions;  renderFunctionsTab(state, index, filter);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Compilands")) { state->activeBrowserTab = BrowserTab::Compilands; renderCompilandsTab(state, index, filter); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Typedefs"))   { state->activeBrowserTab = BrowserTab::Typedefs;   renderTypedefsTab(state, index, filter);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Globals"))    { state->activeBrowserTab = BrowserTab::Globals;    renderGlobalsTab(state, index, filter);    ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::End();
}
