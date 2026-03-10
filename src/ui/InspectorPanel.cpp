#include "InspectorPanel.h"
#include "app/AppState.h"
#include "codegen/CodeGen.h"
#include "pdb/PdbIndex.h"
#include "pdb/Prettify.h"
#include "pdb/SymbolNode.h"

#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <format>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#endif

// ── source file helpers ──────────────────────────────────────────────────────

static bool isFileLocal(AppState* state, const std::string& path)
{
    if (path.empty()) return false;
    auto it = state->fileExistsCache.find(path);
    if (it != state->fileExistsCache.end()) return it->second;
    bool exists = false;
    try { exists = std::filesystem::exists(path); } catch (...) {}
    state->fileExistsCache[path] = exists;
    return exists;
}

static void openFileInEditor(const std::string& path)
{
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

// Renders a source file path as a clickable link (if the file exists locally)
// or as text with a red underline + tooltip (if not found).
static void renderSourceLink(AppState* state, const std::string& path, uint32_t line = 0)
{
    if (path.empty()) return;

    bool local = isFileLocal(state, path);
    std::string displayText = line > 0
        ? std::format("{} : {}", path, line)
        : path;

    if (local) {
        ImVec4 linkColor = ImGui::GetStyle().Colors[ImGuiCol_HeaderActive];
        ImGui::PushStyleColor(ImGuiCol_Text, linkColor);
        ImGui::TextUnformatted(displayText.c_str());
        ImGui::PopStyleColor();

        // Underline
        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 mx = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(
            {mn.x, mx.y}, {mx.x, mx.y}, ImGui::GetColorU32(linkColor), 1.0f);

        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Click to open in editor");
        }
        if (ImGui::IsItemClicked())
            openFileInEditor(path);
    } else {
        ImGui::TextUnformatted(displayText.c_str());

        // Subtle red underline
        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 mx = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(
            {mn.x, mx.y}, {mx.x, mx.y}, IM_COL32(200, 80, 80, 160), 1.0f);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("File not found locally:\n%s", path.c_str());
    }
}

// ── sub-renderers ─────────────────────────────────────────────────────────────

static void renderCompiland(const SymbolNode& sym, AppState* state)
{
    const auto& index = *state->activeIndex;
    const auto& comp = std::get<CompilandData>(sym.kindData);
    ImGui::SeparatorText("Compiland");

    auto row = [](const char* label, const char* value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(value);
    };

    // ── Properties table ─────────────────────────────────────────────────────
    if (ImGui::BeginTable("CompilandTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 130.f);
        ImGui::TableSetupColumn("Value");

        row("Object file", sym.name.c_str());

        if (!sym.typeName.empty())
            row("Library", sym.typeName.c_str());

        auto childrenStr = std::format("{}", sym.children.size());
        row("Symbols", childrenStr.c_str());

        auto srcFilesStr = std::format("{}", comp.sourceFiles.size());
        row("Source files", srcFilesStr.c_str());

        ImGui::EndTable();
    }

    // ── Source files (collapsed) ─────────────────────────────────────────────
    if (!comp.sourceFiles.empty()) {
        ImGui::Spacing();
        auto srcHeader = std::format("Source Files ({})", comp.sourceFiles.size());
        if (ImGui::CollapsingHeader(srcHeader.c_str())) {
            for (const auto& sf : comp.sourceFiles) {
                ImGui::Bullet();
                ImGui::SameLine();
                renderSourceLink(state, sf);
            }
        }
    }

    // ── Children (functions + data) ──────────────────────────────────────────
    if (!sym.children.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Symbols");
        if (ImGui::BeginTable("CompilandChildren", 3,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
                {0, 350}))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 50.f);
            ImGui::TableSetupColumn("RVA",  ImGuiTableColumnFlags_WidthFixed, 90.f);
            ImGui::TableHeadersRow();

            for (SymbolId childId : sym.children) {
                const SymbolNode* child = index.getSymbol(childId);
                if (!child) continue;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                auto dn = displayName(*child, state->prettifyNames);
                ImGui::PushID(childId);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
                if (ImGui::SmallButton(dn.empty() ? "<unnamed>" : std::string(dn).c_str()))
                    state->selectSymbol(childId);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Click to inspect %.*s", static_cast<int>(dn.size()), dn.data());
                ImGui::PopID();

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(child->kind == SymbolKind::Function ? "FN" : "VAR");

                ImGui::TableSetColumnIndex(2);
                if (child->rva != 0) {
                    auto rvaStr = std::format("0x{:08X}", child->rva);
                    ImGui::TextUnformatted(rvaStr.c_str());
                }
            }
            ImGui::EndTable();
        }
    }
}

static void renderTemplateArgs(const SymbolNode& sym, AppState* state)
{
    if (sym.templateArgs.empty()) return;
    const auto& index = *state->activeIndex;

    ImGui::Spacing();
    ImGui::SeparatorText("Template Arguments");

    for (size_t i = 0; i < sym.templateArgs.size(); ++i) {
        const auto& ta = sym.templateArgs[i];
        auto argDn = displayTypeName(ta.text, ta.prettyText, state->prettifyNames);

        ImGui::Bullet();
        ImGui::SameLine();

        if (ta.typeId != INVALID_SYMBOL_ID) {
            const SymbolNode* typeSym = index.getSymbol(ta.typeId);
            if (typeSym) {
                ImGui::PushID(static_cast<int>(i) + 200000);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
                if (ImGui::SmallButton(std::string(argDn).c_str()))
                    state->selectSymbol(ta.typeId);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    auto dn = displayName(*typeSym, state->prettifyNames);
                    ImGui::SetTooltip("Click to inspect %.*s", static_cast<int>(dn.size()), dn.data());
                }
                ImGui::PopID();
                continue;
            }
        }
        ImGui::TextUnformatted(std::string(argDn).c_str());
    }
}

static const char* callingConvLabel(CallingConv cc)
{
    switch (cc) {
    case CallingConv::CDecl:    return "__cdecl";
    case CallingConv::StdCall:  return "__stdcall";
    case CallingConv::FastCall: return "__fastcall";
    case CallingConv::ThisCall: return "__thiscall";
    case CallingConv::ClrCall:  return "__clrcall";
    case CallingConv::Other:    return "(other)";
    default:                    return "";
    }
}

static void renderFunction(const SymbolNode& sym, AppState* state)
{
    const auto& index = *state->activeIndex;
    const auto& fn = std::get<FunctionData>(sym.kindData);
    ImGui::SeparatorText("Function");

    auto row = [](const char* label, const char* value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(value);
    };

    if (ImGui::BeginTable("FnTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 130.f);
        ImGui::TableSetupColumn("Value");

        auto rvaStr  = std::format("0x{:08X}", sym.rva);
        auto sizeStr = std::format("{} bytes", sym.sizeBytes);
        row("RVA",  rvaStr.c_str());
        row("Size", sizeStr.c_str());

        // Calling convention
        if (fn.callingConvention != CallingConv::Unknown)
            row("Calling conv.", callingConvLabel(fn.callingConvention));

        // Return type (clickable if navigable)
        if (!sym.typeName.empty()) {
            bool navigable = false;
            const SymbolNode* retSym = nullptr;
            if (sym.typeId != INVALID_SYMBOL_ID) {
                retSym = index.getSymbol(sym.typeId);
                if (retSym) {
                    using enum SymbolKind;
                    navigable = retSym->kind == UDT || retSym->kind == Enum || retSym->kind == Typedef;
                }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Return type");
            ImGui::TableSetColumnIndex(1);
            auto retTypeDn = displayTypeName(sym.typeName, sym.prettyTypeName, state->prettifyNames);
            if (navigable) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
                if (ImGui::SmallButton(std::string(retTypeDn).c_str()))
                    state->selectSymbol(sym.typeId);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    auto retDn = displayName(*retSym, state->prettifyNames);
                    ImGui::SetTooltip("Click to inspect %.*s", static_cast<int>(retDn.size()), retDn.data());
                }
            } else {
                ImGui::TextUnformatted(std::string(retTypeDn).c_str());
            }
        }

        row("Inline",   fn.isInline   ? "yes" : "no");
        row("Virtual",  fn.isVirtual  ? "yes" : "no");
        row("Pure",     fn.isPure     ? "yes" : "no");
        row("Static",   fn.isStatic   ? "yes" : "no");
        if (fn.isNoReturn) row("No-return", "yes");

        // Parent class (clickable)
        if (sym.parentId != INVALID_SYMBOL_ID) {
            const SymbolNode* parent = index.getSymbol(sym.parentId);
            if (parent) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Parent class");
                ImGui::TableSetColumnIndex(1);
                auto parentDn = displayName(*parent, state->prettifyNames);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
                if (ImGui::SmallButton(std::string(parentDn).c_str()))
                    state->selectSymbol(sym.parentId);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Click to inspect %.*s", static_cast<int>(parentDn.size()), parentDn.data());
            }
        }

        if (!sym.sourceFile.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Source");
            ImGui::TableSetColumnIndex(1);
            renderSourceLink(state, sym.sourceFile, sym.sourceLine);
        }

        ImGui::EndTable();
    }

    renderTemplateArgs(sym, state);
}

// Max data rows visible before a table becomes scrollable.
constexpr int MAX_TABLE_ROWS = 15;

// Returns a table outer height that fits exactly `rowCount` data rows (plus the
// header row), capped at MAX_TABLE_ROWS.  Passing 0 means "no table needed".
static float autoTableHeight(int rowCount)
{
    if (rowCount <= 0) return 0.f;
    int visible = std::min(rowCount, MAX_TABLE_ROWS);
    float rowH  = ImGui::GetTextLineHeightWithSpacing();
    // +1 for the header row, small pad so the last row isn't clipped
    return rowH * (visible + 1) + ImGui::GetStyle().CellPadding.y * 2.f;
}

static void renderUDT(const SymbolNode& sym, AppState* state)
{
    const auto& index = *state->activeIndex;
    const auto& udt = std::get<UdtData>(sym.kindData);

    ImGui::SeparatorText(udtTagStr(udt.udtTag));

    ImGui::Text("Size: %u bytes", static_cast<unsigned>(sym.sizeBytes));

    renderTemplateArgs(sym, state);

    // ── Base classes ─────────────────────────────────────────────────────────
    if (!udt.baseClasses.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Base Classes");
        for (SymbolId bcId : udt.baseClasses) {
            const SymbolNode* bc = index.getSymbol(bcId);
            if (!bc) continue;
            ImGui::Bullet();
            ImGui::SameLine();
            auto bcDn = displayName(*bc, state->prettifyNames);
            ImGui::PushID(bcId);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
            if (ImGui::SmallButton(std::string(bcDn).c_str()))
                state->selectSymbol(bcId);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to inspect %.*s", static_cast<int>(bcDn.size()), bcDn.data());
            ImGui::PopID();
        }
    }

    // ── Friends ──────────────────────────────────────────────────────────────
    if (!udt.friends.empty()) {
        ImGui::TextDisabled("Friends:");
        for (size_t i = 0; i < udt.friends.size(); ++i) {
            ImGui::SameLine();
            const SymbolNode* fr = index.getSymbol(udt.friends[i]);
            if (!fr) continue;
            auto frDn = displayName(*fr, state->prettifyNames);
            ImGui::PushID(static_cast<int>(i) + 100000); // unique within panel
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
            if (ImGui::SmallButton(std::string(frDn).c_str()))
                state->selectSymbol(udt.friends[i]);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to inspect %.*s", static_cast<int>(frDn.size()), frDn.data());
            ImGui::PopID();
            if (i + 1 < udt.friends.size()) {
                ImGui::SameLine(0, 0);
                ImGui::TextUnformatted(",");
            }
        }
    }

    // ── Data members ─────────────────────────────────────────────────────────
    if (!udt.members.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Data Members");
        float tableH = autoTableHeight(static_cast<int>(udt.members.size()));
        if (ImGui::BeginTable("Members", 4,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
                {0, tableH}))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 70.f);
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Size",   ImGuiTableColumnFlags_WidthFixed, 70.f);
            ImGui::TableHeadersRow();

            for (size_t mi = 0; mi < udt.members.size(); ++mi) {
                const auto& m = udt.members[mi];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (m.bitSize > 0) {
                    auto s = std::format("+{} [{}:{}]", m.offset, m.bitOffset, m.bitSize);
                    ImGui::TextUnformatted(s.c_str());
                } else {
                    auto s = std::format("+{}", m.offset);
                    ImGui::TextUnformatted(s.c_str());
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(m.name.c_str());
                ImGui::TableSetColumnIndex(2);
                // Make type clickable if it resolves to an inspectable symbol
                const SymbolNode* typeSym = nullptr;
                bool navigable = false;
                if (m.typeId != INVALID_SYMBOL_ID) {
                    typeSym = index.getSymbol(m.typeId);
                    if (typeSym) {
                        using enum SymbolKind;
                        navigable = typeSym->kind == UDT || typeSym->kind == Enum || typeSym->kind == Typedef;
                    }
                }
                auto memberTypeDn = displayTypeName(m.typeName, m.prettyTypeName, state->prettifyNames);
                if (navigable) {
                    ImGui::PushID(static_cast<int>(mi));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
                    if (ImGui::SmallButton(std::string(memberTypeDn).c_str()))
                        state->selectSymbol(m.typeId);
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) {
                        auto typeDn = displayName(*typeSym, state->prettifyNames);
                        ImGui::SetTooltip("Click to inspect %.*s", static_cast<int>(typeDn.size()), typeDn.data());
                    }
                    ImGui::PopID();
                } else {
                    ImGui::TextUnformatted(std::string(memberTypeDn).c_str());
                }
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%u", m.sizeBytes);
            }
            ImGui::EndTable();
        }
    }

    // ── Member functions ─────────────────────────────────────────────────────
    if (!sym.children.empty()) {
        // Count functions for auto-sizing
        int fnCount = 0;
        for (SymbolId cid : sym.children) {
            const SymbolNode* fn = index.getSymbol(cid);
            if (fn && fn->kind == SymbolKind::Function) ++fnCount;
        }

        if (fnCount > 0) {
            ImGui::Spacing();
            ImGui::SeparatorText("Member Functions");
            float tableH = autoTableHeight(fnCount);
            if (ImGui::BeginTable("MemberFuncs", 5,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                    ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
                    {0, tableH}))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Virtual", ImGuiTableColumnFlags_WidthFixed, 55.f);
                ImGui::TableSetupColumn("Pure",    ImGuiTableColumnFlags_WidthFixed, 40.f);
                ImGui::TableSetupColumn("Static",  ImGuiTableColumnFlags_WidthFixed, 50.f);
                ImGui::TableSetupColumn("RVA",     ImGuiTableColumnFlags_WidthFixed, 90.f);
                ImGui::TableHeadersRow();

                for (SymbolId childId : sym.children) {
                    const SymbolNode* fn = index.getSymbol(childId);
                    if (!fn || fn->kind != SymbolKind::Function) continue;

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    auto fnDn = displayName(*fn, state->prettifyNames);
                    ImGui::PushID(childId);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
                    if (ImGui::SmallButton(std::string(fnDn).c_str()))
                        state->selectSymbol(childId);
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Click to inspect %.*s", static_cast<int>(fnDn.size()), fnDn.data());
                    ImGui::PopID();

                    const auto& fnData = std::get<FunctionData>(fn->kindData);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(fnData.isVirtual ? "*" : "");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(fnData.isPure    ? "*" : "");
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(fnData.isStatic  ? "*" : "");
                    ImGui::TableSetColumnIndex(4);
                    if (fn->rva != 0) {
                        auto rvaStr = std::format("0x{:08X}", fn->rva);
                        ImGui::TextUnformatted(rvaStr.c_str());
                    }
                }
                ImGui::EndTable();
            }
        }
    }

    // ── Virtual function table (collapsed) ───────────────────────────────────
    if (!sym.children.empty()) {
        std::vector<SymbolId> vfuncs;
        for (SymbolId childId : sym.children) {
            const SymbolNode* fn = index.getSymbol(childId);
            if (fn && fn->kind == SymbolKind::Function
                && std::get<FunctionData>(fn->kindData).isVirtual)
                vfuncs.push_back(childId);
        }

        if (!vfuncs.empty()) {
            ImGui::Spacing();
            auto vtableHeader = std::format("VTable ({} entries)", vfuncs.size());
            if (ImGui::CollapsingHeader(vtableHeader.c_str())) {
                float tableH = autoTableHeight(static_cast<int>(vfuncs.size()));
                if (ImGui::BeginTable("VTable", 2,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
                        {0, tableH}))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Pure", ImGuiTableColumnFlags_WidthFixed, 40.f);
                    ImGui::TableHeadersRow();

                    for (SymbolId vfId : vfuncs) {
                        const SymbolNode* fn = index.getSymbol(vfId);
                        if (!fn) continue;

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        auto vfDn = displayName(*fn, state->prettifyNames);
                        ImGui::PushID(vfId);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
                        if (ImGui::SmallButton(std::string(vfDn).c_str()))
                            state->selectSymbol(vfId);
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Click to inspect %.*s", static_cast<int>(vfDn.size()), vfDn.data());
                        ImGui::PopID();

                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(std::get<FunctionData>(fn->kindData).isPure ? "yes" : "");
                    }
                    ImGui::EndTable();
                }
            }
        }
    }
}

static void renderEnum(const SymbolNode& sym, AppState* state)
{
    const auto& en = std::get<EnumData>(sym.kindData);
    ImGui::SeparatorText("Enum");

    ImGui::Text("Size: %u bytes", sym.sizeBytes);
    if (!sym.typeName.empty()) {
        auto underlyingDn = displayTypeName(sym.typeName, sym.prettyTypeName, state->prettifyNames);
        ImGui::Text("Underlying type: %.*s", static_cast<int>(underlyingDn.size()), underlyingDn.data());
    }
    if (!en.enumValues.empty()) {
        ImGui::Spacing();
        ImGui::Text("%zu enumerators:", en.enumValues.size());
        if (ImGui::BeginTable("EnumVals", 2,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
                {0, 300}))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 120.f);
            ImGui::TableHeadersRow();

            for (const auto& ev : en.enumValues) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(ev.name.c_str());
                ImGui::TableSetColumnIndex(1);
                auto valStr = std::format("{} (0x{:X})", ev.value, ev.value);
                ImGui::TextUnformatted(valStr.c_str());
            }
            ImGui::EndTable();
        }
    }
}

static void renderTypedef(const SymbolNode& sym, AppState* state)
{
    ImGui::SeparatorText("Typedef");

    auto row = [](const char* label, const char* value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(value);
    };

    if (ImGui::BeginTable("TypedefTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 130.f);
        ImGui::TableSetupColumn("Value");

        {
            auto aliasDn = displayName(sym, state->prettifyNames);
            row("Alias", std::string(aliasDn).c_str());
        }

        if (sym.typeId != INVALID_SYMBOL_ID) {
            const SymbolNode* resolved = state->activeIndex->getSymbol(sym.typeId);
            auto resolvedTypeDn = displayTypeName(sym.typeName, sym.prettyTypeName, state->prettifyNames);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Resolves to");
            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
            if (ImGui::SmallButton(std::string(resolvedTypeDn).c_str()))
                state->selectSymbol(sym.typeId);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to inspect %.*s", static_cast<int>(resolvedTypeDn.size()), resolvedTypeDn.data());

            if (resolved && resolved->sizeBytes > 0) {
                auto sizeStr = std::format("{} bytes", resolved->sizeBytes);
                row("Size", sizeStr.c_str());
            }
        } else if (!sym.typeName.empty()) {
            auto resolvedTypeDn = displayTypeName(sym.typeName, sym.prettyTypeName, state->prettifyNames);
            row("Resolves to", std::string(resolvedTypeDn).c_str());
        }

        ImGui::EndTable();
    }
}

static const char* dataKindLabel(uint32_t dk)
{
    switch (dk) {
    case 0:  return "Unknown";
    case 1:  return "Local";
    case 2:  return "Static Local";
    case 3:  return "Param";
    case 4:  return "Object Ptr";
    case 5:  return "File Static";
    case 6:  return "Global";
    case 7:  return "Member";
    case 8:  return "Static Member";
    case 9:  return "Constant";
    default: return "Other";
    }
}

static void renderData(const SymbolNode& sym, AppState* state)
{
    const auto& index = *state->activeIndex;
    const auto& dat = std::get<DataSymData>(sym.kindData);
    ImGui::SeparatorText("Data");

    auto row = [](const char* label, const char* value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(value);
    };

    if (ImGui::BeginTable("DataTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 130.f);
        ImGui::TableSetupColumn("Value");

        row("Data kind", dataKindLabel(dat.dataKind));

        if (sym.rva != 0) {
            auto rvaStr = std::format("0x{:08X}", sym.rva);
            row("RVA", rvaStr.c_str());
        }

        // Type (clickable if navigable)
        if (!sym.typeName.empty()) {
            const SymbolNode* typeSym = nullptr;
            bool navigable = false;
            if (sym.typeId != INVALID_SYMBOL_ID) {
                typeSym = index.getSymbol(sym.typeId);
                if (typeSym) {
                    using enum SymbolKind;
                    navigable = typeSym->kind == UDT || typeSym->kind == Enum || typeSym->kind == Typedef;
                }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Type");
            ImGui::TableSetColumnIndex(1);
            auto typeDn = displayTypeName(sym.typeName, sym.prettyTypeName, state->prettifyNames);
            if (navigable) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]);
                if (ImGui::SmallButton(std::string(typeDn).c_str()))
                    state->selectSymbol(sym.typeId);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    auto dn = displayName(*typeSym, state->prettifyNames);
                    ImGui::SetTooltip("Click to inspect %.*s", static_cast<int>(dn.size()), dn.data());
                }
            } else {
                ImGui::TextUnformatted(std::string(typeDn).c_str());
            }
        }

        // Size — always show
        auto sizeStr = std::format("{} bytes", sym.sizeBytes);
        row("Size", sizeStr.c_str());

        // Value — show when available (constants)
        if (dat.hasConstValue) {
            auto valStr = std::format("{} (0x{:X})", dat.constValue, dat.constValue);
            row("Value", valStr.c_str());
        }

        // Source location
        if (!sym.sourceFile.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Source");
            ImGui::TableSetColumnIndex(1);
            renderSourceLink(state, sym.sourceFile, sym.sourceLine);
        }

        ImGui::EndTable();
    }
}

// ── InspectorPanel ───────────────────────────────────────────────────────────

void InspectorPanel::render(AppState* state)
{
    ImGui::Begin("Inspector");

    // ── Navigation buttons ──────────────────────────────────────────────────
    {
        bool canBack = state->canNavigateBack();
        bool canFwd  = state->canNavigateForward();

        if (!canBack) ImGui::BeginDisabled();
        if (ImGui::ArrowButton("##NavBack", ImGuiDir_Left))
            state->navigateBack();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Back (Mouse4 / Alt+Left)");
        if (!canBack) ImGui::EndDisabled();

        ImGui::SameLine();

        if (!canFwd) ImGui::BeginDisabled();
        if (ImGui::ArrowButton("##NavFwd", ImGuiDir_Right))
            state->navigateForward();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Forward (Mouse5 / Alt+Right)");
        if (!canFwd) ImGui::EndDisabled();

        ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }

    if (!state->activeIndex || state->selectedSymbol == INVALID_SYMBOL_ID) {
        ImGui::NewLine();
        ImGui::TextDisabled("Select a symbol to inspect.");
        ImGui::End();
        return;
    }

    const SymbolNode* sym = state->activeIndex->getSymbol(state->selectedSymbol);
    if (!sym) {
        ImGui::NewLine();
        ImGui::TextDisabled("Symbol not found.");
        ImGui::End();
        return;
    }

    // ── "View as Code" button (right-aligned on nav button line) ────────────
    auto langs = applicableLanguages(*sym);
    if (!langs.empty()) {
        // Right-align the button on the current line
        float buttonWidth = ImGui::CalcTextSize("</>").x + ImGui::GetStyle().FramePadding.x * 2.f;
        float available   = ImGui::GetContentRegionAvail().x;
        ImGui::SameLine(available - buttonWidth);

        if (ImGui::Button("</>")) {
            m_applicableLangs = std::move(langs);
            m_selectedLang    = 0;
            m_generatedCode   = generateCode(*state->activeIndex, *sym,
                                                m_applicableLangs[0], state->prettifyNames);
            m_showCodePopup = true;
            ImGui::OpenPopup("##CodeViewPopup");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("View as code declaration");
    }

    renderCodePopup(state);

    // Header — raw name as title, prettified name as secondary
    const std::string& rawName =
        sym->undecoratedName.empty() ? sym->name : sym->undecoratedName;
    ImGui::TextWrapped("%s", rawName.empty() ? "<unnamed>" : rawName.c_str());
    // Show prettified name underneath when it differs from raw
    if (state->prettifyNames && !sym->prettyName.empty() && sym->prettyName != rawName) {
        ImGui::TextDisabled("%s", sym->prettyName.c_str());
    }
    auto idStr = std::format("[{}]  pdbId: {}  kind: {}", sym->id, sym->pdbId, kindStr(sym->kind));
    ImGui::TextDisabled("%s", idStr.c_str());
    ImGui::Separator();

    switch (sym->kind) {
    case SymbolKind::Compiland:
        renderCompiland(*sym, state);
        break;
    case SymbolKind::Function:
        renderFunction(*sym, state);
        break;
    case SymbolKind::UDT:
        renderUDT(*sym, state);
        break;
    case SymbolKind::Enum:
        renderEnum(*sym, state);
        break;
    case SymbolKind::Typedef:
        renderTypedef(*sym, state);
        break;
    case SymbolKind::Data:
        renderData(*sym, state);
        break;
    default:
        ImGui::TextDisabled("(no detail view for this symbol kind)");
        break;
    }

    ImGui::End();
}

// ── Code view popup ──────────────────────────────────────────────────────────

void InspectorPanel::renderCodePopup(AppState* state)
{
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("##CodeViewPopup", &m_showCodePopup)) return;

    const SymbolNode* sym = state->activeIndex->getSymbol(state->selectedSymbol);

    // Language selector
    if (!m_applicableLangs.empty()) {
        ImGui::SetNextItemWidth(100);
        if (ImGui::BeginCombo("##Lang", codeLangLabel(m_applicableLangs[m_selectedLang]))) {
            for (int i = 0; i < static_cast<int>(m_applicableLangs.size()); ++i) {
                bool selected = (i == m_selectedLang);
                if (ImGui::Selectable(codeLangLabel(m_applicableLangs[i]), selected)) {
                    m_selectedLang = i;
                    if (sym)
                        m_generatedCode = generateCode(*state->activeIndex, *sym,
                                                       m_applicableLangs[i], state->prettifyNames);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Copy"))
            ImGui::SetClipboardText(m_generatedCode.c_str());
    }

    ImGui::Separator();

    // Code text (read-only, monospace feel via InputTextMultiline)
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InputTextMultiline("##CodeText", m_generatedCode.data(),
                              m_generatedCode.size() + 1, avail,
                              ImGuiInputTextFlags_ReadOnly);

    ImGui::EndPopup();
}
