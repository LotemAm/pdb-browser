#include "BrowserPanel.h"
#include "app/AppState.h"
#include "pdb/PdbIndex.h"
#include "pdb/Prettify.h"
#include "pdb/SymbolNode.h"

#include <imgui.h>
#include <algorithm>
#include <cstring>

// ── helpers ──────────────────────────────────────────────────────────────────

// Case-insensitive substring search. Returns true if `needle` is found in `haystack`.
static bool matchesFilter(std::string_view haystack, const char* needle)
{
    if (!needle || needle[0] == '\0') return true;
    if (haystack.empty()) return false;
    // Simple case-insensitive search using std::search with a char-compare lambda
    std::string_view nv(needle);
    auto it = std::search(haystack.begin(), haystack.end(),
                          nv.begin(), nv.end(),
                          [](char a, char b) {
                              return std::tolower(static_cast<unsigned char>(a)) ==
                                     std::tolower(static_cast<unsigned char>(b));
                          });
    return it != haystack.end();
}

// Check if any of the symbol's name fields match the filter.
// Searches raw name, undecorated name, and (when prettify is on) the prettified name.
static bool symbolMatchesFilter(const SymbolNode& node, const char* filter, bool prettify)
{
    if (!filter || filter[0] == '\0') return true;
    if (matchesFilter(node.name, filter)) return true;
    if (!node.undecoratedName.empty() && matchesFilter(node.undecoratedName, filter)) return true;
    if (prettify && !node.prettyName.empty() && matchesFilter(node.prettyName, filter)) return true;
    return false;
}

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
        // Lazy-load children on first expand
        // (actual DIA call would happen here via a callback / queue;
        //  for now children must be pre-populated by the indexer)
        if (state->activeIndex) {
            for (SymbolId childId : node->children) {
                const SymbolNode* child = state->activeIndex->getSymbol(childId);
                renderSymbolNode(state, child);
            }
        }
        ImGui::TreePop();
    }
}

// ── tab renderers ────────────────────────────────────────────────────────────

static void renderTypesTab(AppState* state, PdbIndex* index, const char* filter)
{
    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("TypesTable", 3, tableFlags))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Size (bytes)", ImGuiTableColumnFlags_WidthFixed, 100.f);
    ImGui::TableSetupColumn("Loaded",       ImGuiTableColumnFlags_WidthFixed, 55.f);
    ImGui::TableHeadersRow();

    for (SymbolId id : index->udts()) {
        const SymbolNode* node = index->getSymbol(id);
        if (!node) continue;
        if (!symbolMatchesFilter(*node, filter, state->prettifyNames)) continue;
        auto dn = displayName(*node, state->prettifyNames);

        ImGui::TableNextRow();

        // Name (selectable spanning all columns)
        ImGui::TableSetColumnIndex(0);
        bool selected = (node->id == state->selectedSymbol);
        ImGui::PushID(static_cast<int>(node->id));
        if (ImGui::Selectable(
                node->name.empty() ? "<unnamed>" : std::string(dn).c_str(),
                selected, ImGuiSelectableFlags_SpanAllColumns))
            state->selectSymbol(node->id);
        ImGui::PopID();

        // Size
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%u", node->sizeBytes);

        // Loaded indicator
        ImGui::TableSetColumnIndex(2);
        if (node->childrenLoaded)
            ImGui::TextUnformatted("*");
    }

    ImGui::EndTable();
}

static void renderEnumsTab(AppState* state, PdbIndex* index, const char* filter)
{
    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("EnumsTable", 2, tableFlags))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Loaded", ImGuiTableColumnFlags_WidthFixed, 55.f);
    ImGui::TableHeadersRow();

    for (SymbolId id : index->enums()) {
        const SymbolNode* node = index->getSymbol(id);
        if (!node) continue;
        if (!symbolMatchesFilter(*node, filter, state->prettifyNames)) continue;
        auto dn = displayName(*node, state->prettifyNames);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool selected = (node->id == state->selectedSymbol);
        ImGui::PushID(static_cast<int>(node->id));
        if (ImGui::Selectable(
                node->name.empty() ? "<unnamed>" : std::string(dn).c_str(),
                selected, ImGuiSelectableFlags_SpanAllColumns))
            state->selectSymbol(node->id);
        ImGui::PopID();

        ImGui::TableSetColumnIndex(1);
        if (node->childrenLoaded)
            ImGui::TextUnformatted("*");
    }

    ImGui::EndTable();
}

static void renderCompilandsTab(AppState* state, PdbIndex* index, const char* filter)
{
    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("CompilandsTable", 2, tableFlags))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Loaded", ImGuiTableColumnFlags_WidthFixed, 55.f);
    ImGui::TableHeadersRow();

    for (SymbolId id : index->compilands()) {
        const SymbolNode* node = index->getSymbol(id);
        if (!node) continue;
        if (!symbolMatchesFilter(*node, filter, state->prettifyNames)) continue;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool selected = (node->id == state->selectedSymbol);
        ImGui::PushID(static_cast<int>(node->id));
        if (ImGui::Selectable(
                node->name.empty() ? "<unnamed>" : node->name.c_str(),
                selected, ImGuiSelectableFlags_SpanAllColumns))
            state->selectSymbol(node->id);
        ImGui::PopID();

        ImGui::TableSetColumnIndex(1);
        if (node->childrenLoaded)
            ImGui::TextUnformatted("*");
    }

    ImGui::EndTable();
}

static void renderTypedefsTab(AppState* state, PdbIndex* index, const char* filter)
{
    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("TypedefsTable", 2, tableFlags))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Loaded", ImGuiTableColumnFlags_WidthFixed, 55.f);
    ImGui::TableHeadersRow();

    for (SymbolId id : index->typedefs()) {
        const SymbolNode* node = index->getSymbol(id);
        if (!node) continue;
        if (!symbolMatchesFilter(*node, filter, state->prettifyNames)) continue;
        auto dn = displayName(*node, state->prettifyNames);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool selected = (node->id == state->selectedSymbol);
        ImGui::PushID(static_cast<int>(node->id));
        if (ImGui::Selectable(
                node->name.empty() ? "<unnamed>" : std::string(dn).c_str(),
                selected, ImGuiSelectableFlags_SpanAllColumns))
            state->selectSymbol(node->id);
        ImGui::PopID();

        ImGui::TableSetColumnIndex(1);
        if (node->childrenLoaded)
            ImGui::TextUnformatted("*");
    }

    ImGui::EndTable();
}

static void renderGlobalsTab(AppState* state, PdbIndex* index, const char* filter)
{
    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("GlobalsTable", 1, tableFlags))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name");
    ImGui::TableHeadersRow();

    for (SymbolId id : index->globals()) {
        const SymbolNode* node = index->getSymbol(id);
        if (!node) continue;
        if (!symbolMatchesFilter(*node, filter, state->prettifyNames)) continue;
        auto dn = displayName(*node, state->prettifyNames);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool selected = (node->id == state->selectedSymbol);
        ImGui::PushID(static_cast<int>(node->id));
        if (ImGui::Selectable(
                node->name.empty() ? "<unnamed>" : std::string(dn).c_str(),
                selected, ImGuiSelectableFlags_SpanAllColumns))
            state->selectSymbol(node->id);
        ImGui::PopID();
    }

    ImGui::EndTable();
}

static void renderFunctionsTab(AppState* state, PdbIndex* index, const char* filter)
{
    constexpr ImGuiTableFlags tableFlags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("FunctionsTable", 4, tableFlags))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("RVA",    ImGuiTableColumnFlags_WidthFixed, 90.f);
    ImGui::TableSetupColumn("Size",   ImGuiTableColumnFlags_WidthFixed, 80.f);
    ImGui::TableSetupColumn("Loaded", ImGuiTableColumnFlags_WidthFixed, 55.f);
    ImGui::TableHeadersRow();

    for (SymbolId id : index->functions()) {
        const SymbolNode* node = index->getSymbol(id);
        if (!node) continue;
        if (!symbolMatchesFilter(*node, filter, state->prettifyNames)) continue;
        auto dn = displayName(*node, state->prettifyNames);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool selected = (node->id == state->selectedSymbol);
        ImGui::PushID(static_cast<int>(node->id));
        if (ImGui::Selectable(
                node->name.empty() ? "<unnamed>" : std::string(dn).c_str(),
                selected, ImGuiSelectableFlags_SpanAllColumns))
            state->selectSymbol(node->id);
        ImGui::PopID();

        ImGui::TableSetColumnIndex(1);
        if (node->rva != 0)
            ImGui::Text("0x%08X", node->rva);

        ImGui::TableSetColumnIndex(2);
        if (node->sizeBytes > 0)
            ImGui::Text("%u", node->sizeBytes);

        ImGui::TableSetColumnIndex(3);
        if (node->childrenLoaded)
            ImGui::TextUnformatted("*");
    }

    ImGui::EndTable();
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
