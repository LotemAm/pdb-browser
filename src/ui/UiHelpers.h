#pragma once
#include "pdb/SymbolNode.h"

#include <imgui.h>
#include <string_view>

struct AppState;

// Null-terminate a string_view cheaply.  All views returned by displayName()
// and displayTypeName() point into std::string members or string literals,
// so they are already null-terminated and we can use .data() directly.
// This helper documents and enforces that invariant with a debug-only check.
inline const char* sv_cstr(std::string_view sv)
{
    // Assumes caller only passes views into null-terminated storage.
    // In debug builds we can assert; in release we trust the invariant.
#ifndef NDEBUG
    if (!sv.empty()) assert(sv.data()[sv.size()] == '\0');
#endif
    return sv.empty() ? "" : sv.data();
}

// ── Text search ──────────────────────────────────────────────────────────────

// Case-insensitive substring search.
bool caseInsensitiveContains(std::string_view haystack, std::string_view needle);

// Check if any of a symbol's name fields match the filter.
// Searches raw name, undecorated name, and (when prettify is on) the prettified name.
bool symbolMatchesFilter(const SymbolNode& node, const char* filter, bool prettify);

// ── ImGui helpers ────────────────────────────────────────────────────────────

// Common table flags for browser-style scrollable tables.
constexpr ImGuiTableFlags kBrowserTableFlags =
    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
    ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
    ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

// Render a selectable row in a browser table.  Returns true if clicked.
// Pushes/pops ID internally.
bool renderSelectableSymbolRow(AppState* state, const SymbolNode& node,
                               std::string_view displayLabel);

// Render a clickable SmallButton that navigates to `targetId` when clicked.
// Shows a tooltip on hover.  Pushes/pops ID and style color internally.
void renderSymbolButton(AppState* state, SymbolId targetId,
                        std::string_view label, std::string_view tooltipName,
                        int pushIdHint = -1);

// Render a clickable SmallButton or plain text depending on whether `targetId`
// resolves to a navigable symbol.  Wraps renderSymbolButton + fallback.
void renderClickableType(AppState* state, SymbolId typeId,
                         std::string_view displayLabel,
                         int pushIdHint = -1);

// Emit a 2-column property row (label | value) inside an active table.
void propertyRow(const char* label, const char* value);
