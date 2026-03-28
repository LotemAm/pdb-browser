#include "UiHelpers.h"
#include "app/AppState.h"
#include "pdb/PdbIndex.h"
#include "pdb/Prettify.h"

#include <imgui.h>
#include <algorithm>
#include <string>

// ── Text search ──────────────────────────────────────────────────────────────

bool caseInsensitiveContains(std::string_view haystack, std::string_view needle)
{
    if (needle.empty()) return true;
    if (haystack.empty()) return false;
    auto it = std::ranges::search(haystack, needle,
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it.begin() != haystack.end();
}

bool symbolMatchesFilter(const SymbolNode& node, const char* filter, bool prettify)
{
    if (!filter || filter[0] == '\0') return true;
    if (caseInsensitiveContains(node.name, filter)) return true;
    if (!node.undecoratedName.empty() && caseInsensitiveContains(node.undecoratedName, filter)) return true;
    if (prettify && !node.prettyName.empty() && caseInsensitiveContains(node.prettyName, filter)) return true;
    return false;
}

// ── ImGui helpers ────────────────────────────────────────────────────────────

// Resolve through pointer / array chains to find a navigable symbol (UDT, Enum,
// Typedef).  Returns {navigableSymbol, navigableId} or {nullptr, INVALID} if
// the chain doesn't bottom out at something inspectable.
static std::pair<const SymbolNode*, SymbolId> resolveNavigable(
    const PdbIndex& index, SymbolId startId, int maxDepth = 8)
{
    SymbolId cur = startId;
    for (int i = 0; i < maxDepth && cur != INVALID_SYMBOL_ID; ++i) {
        const SymbolNode* s = index.getSymbol(cur);
        if (!s) break;
        using enum SymbolKind;
        if (s->kind == UDT || s->kind == Enum || s->kind == Typedef)
            return {s, cur};
        if (s->kind == PointerType || s->kind == ArrayType) {
            cur = s->typeId;
            continue;
        }
        break;
    }
    return {nullptr, INVALID_SYMBOL_ID};
}

bool renderSelectableSymbolRow(AppState* state, const SymbolNode& node,
                               std::string_view displayLabel)
{
    bool selected = (node.id == state->selectedSymbol);
    ImGui::PushID(static_cast<int>(node.id));
    bool clicked = ImGui::Selectable(
        displayLabel.empty() ? "<unnamed>" : std::string(displayLabel).c_str(),
        selected, ImGuiSelectableFlags_SpanAllColumns);
    if (clicked)
        state->selectSymbol(node.id);
    ImGui::PopID();
    return clicked;
}

void renderSymbolButton(AppState* state, SymbolId targetId,
                        std::string_view label, std::string_view tooltipName,
                        int pushIdHint)
{
    if (pushIdHint >= 0)
        ImGui::PushID(pushIdHint);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
    if (ImGui::SmallButton(label.empty() ? "<unnamed>" : std::string(label).c_str()))
        state->selectSymbol(targetId);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Click to inspect %.*s",
                          static_cast<int>(tooltipName.size()), tooltipName.data());
    if (pushIdHint >= 0)
        ImGui::PopID();
}

void renderClickableType(AppState* state, SymbolId typeId,
                         std::string_view displayLabel,
                         int pushIdHint)
{
    const auto& index = *state->activeIndex;
    auto [navSym, navId] = resolveNavigable(index, typeId);
    if (navSym) {
        auto tooltipName = displayName(*navSym, state->prettifyNames);
        renderSymbolButton(state, navId, displayLabel, tooltipName, pushIdHint);
    } else {
        ImGui::TextUnformatted(std::string(displayLabel).c_str());
    }
}

void propertyRow(const char* label, const char* value)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(value);
}
