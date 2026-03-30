#include "CompFilterDialog.h"
#include "app/AppState.h"
#include "pdb/PdbIndex.h"
#include "pdb/PdbSession.h"
#include "pdb/Prettify.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <format>
#include <thread>

// ── helpers ─────────────────────────────────────────────────────────────────

// Collect all leaf SymbolIds under a tree node (recursively).
static void collectLeafIds(const CompTreeNode& node, std::vector<SymbolId>& out)
{
    for (SymbolId id : node.leafIds)
        out.push_back(id);
    for (const auto& [_, child] : node.children)
        collectLeafIds(child, out);
}

// Count checked/total leaves under a node.
static void countChecked(const CompTreeNode& node, const std::unordered_set<SymbolId>& hidden,
                         int& checked, int& total)
{
    for (SymbolId id : node.leafIds) {
        ++total;
        if (!hidden.contains(id)) ++checked;
    }
    for (const auto& [_, child] : node.children)
        countChecked(child, hidden, checked, total);
}

// ── background resolution ───────────────────────────────────────────────────

static void backgroundResolveCompilands(PdbSession* session, PdbIndex* index,
                                        std::atomic<int>* progress, std::atomic<bool>* resolving)
{
    CoInitialize(nullptr);
    std::atomic<bool> noCancel{false};
    session->resolveAllCompilandIds(*index, *progress, noCancel);
    resolving->store(false);
    CoUninitialize();
}

// ── CompFilterDialog ────────────────────────────────────────────────────────

void CompFilterDialog::buildTree(AppState* state)
{
    m_root = {};
    PdbIndex* index = state->activeIndex.get();
    if (!index) return;

    for (SymbolId id : index->compilands()) {
        const SymbolNode* node = index->getSymbol(id);
        if (!node) continue;

        auto label = displayName(*node, state->prettifyNames);
        std::string_view sv = label;

        // Find last path separator
        CompTreeNode* current = &m_root;
        size_t last_sep = std::string_view::npos;
        for (size_t i = 0; i < sv.size(); ++i) {
            if (sv[i] == '\\' || sv[i] == '/')
                last_sep = i;
        }

        if (last_sep == std::string_view::npos) {
            current->leafIds.push_back(id);
        } else {
            size_t start = 0;
            for (size_t i = 0; i <= last_sep; ++i) {
                if (i == last_sep || sv[i] == '\\' || sv[i] == '/') {
                    if (i > start) {
                        std::string seg(sv.substr(start, i - start));
                        current = &current->children[seg];
                        current->segment = seg;
                    }
                    start = i + 1;
                }
            }
            current->leafIds.push_back(id);
        }
    }
    m_cachedIndex = index;
}

void CompFilterDialog::startResolve(AppState* state)
{
    PdbIndex* index = state->activeIndex.get();
    if (!index || !state->activePdbSession) return;

    m_resolveTotal = static_cast<int>(index->symbolCount());
    m_resolveProgress.store(0);
    m_resolving.store(true);

    std::thread(backgroundResolveCompilands, state->activePdbSession.get(), index,
                &m_resolveProgress, &m_resolving).detach();
}

void CompFilterDialog::renderTreeNode(CompTreeNode& node, AppState* state)
{
    PdbIndex* index = state->activeIndex.get();
    auto& hidden = state->hiddenCompilands;

    // Render child directories as tree nodes
    for (auto& [name, child] : node.children) {
        int checked = 0, total = 0;
        countChecked(child, hidden, checked, total);

        bool allChecked = (checked == total);
        bool noneChecked = (checked == 0);

        if (!allChecked && !noneChecked)
            ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);

        bool groupChecked = !noneChecked;
        if (ImGui::Checkbox(std::format("##{}", (uintptr_t)&child).c_str(), &groupChecked)) {
            std::vector<SymbolId> ids;
            collectLeafIds(child, ids);
            if (groupChecked) {
                for (SymbolId id : ids) hidden.erase(id);
            } else {
                for (SymbolId id : ids) hidden.insert(id);
            }
        }

        if (!allChecked && !noneChecked)
            ImGui::PopItemFlag();

        ImGui::SameLine();
        bool open = ImGui::TreeNode(std::format("{}##{}", name, (uintptr_t)&child).c_str());
        if (open) {
            renderTreeNode(child, state);
            ImGui::TreePop();
        }
    }

    // Render leaf compilands
    for (SymbolId id : node.leafIds) {
        const SymbolNode* sym = index->getSymbol(id);
        if (!sym) continue;
        auto label = displayName(*sym, state->prettifyNames);

        // Extract just the filename (after last separator)
        std::string_view fileName = label;
        auto lastSlash = fileName.find_last_of("\\/");
        if (lastSlash != std::string_view::npos)
            fileName = fileName.substr(lastSlash + 1);

        bool checked = !hidden.contains(id);
        if (ImGui::Checkbox(std::format("##{}", id).c_str(), &checked)) {
            if (checked) hidden.erase(id);
            else         hidden.insert(id);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(fileName.data(), fileName.data() + fileName.size());
    }
}

void CompFilterDialog::render(AppState* state)
{
    if (state->showCompilandFilter && state->activeIndex) {
        ImGui::OpenPopup("Compiland Filter");
        ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_Once);
        state->showCompilandFilter = false;

        // Start background resolution on first open for this index
        if (m_resolvedIndex != static_cast<const void*>(state->activeIndex.get()) && !m_resolving.load()) {
            m_resolved = false;
            startResolve(state);
        }
    }

    // Check if resolution just finished
    if (!m_resolving.load() && !m_resolved &&
        m_resolveTotal > 0 && m_resolveProgress.load() >= m_resolveTotal)
    {
        m_resolved = true;
        m_resolvedIndex = state->activeIndex.get();
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(400, 300), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopupModal("Compiland Filter", nullptr, ImGuiWindowFlags_None)) {
        PdbIndex* index = state->activeIndex.get();
        if (index) {
            if (m_cachedIndex != static_cast<const void*>(index))
                buildTree(state);

            const auto& compilands = index->compilands();

            // Show progress bar while resolving
            if (m_resolving.load()) {
                int done = m_resolveProgress.load();
                float frac = m_resolveTotal > 0 ? static_cast<float>(done) / m_resolveTotal : 0.f;
                ImGui::ProgressBar(frac, ImVec2(-1, 0),
                    std::format("Resolving compilands... {}/{}", done, m_resolveTotal).c_str());
                ImGui::Separator();
            }

            if (ImGui::Button("Check All")) {
                state->hiddenCompilands.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button("Uncheck All")) {
                for (SymbolId id : compilands)
                    state->hiddenCompilands.insert(id);
            }
            ImGui::SameLine();
            ImGui::Checkbox("Hide unassigned symbols", &state->hideUnassignedCompiland);
            ImGui::Separator();

            float reservedHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
            ImGui::BeginChild("##CompList", ImVec2(0, -reservedHeight), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar);
            renderTreeNode(m_root, state);
            ImGui::EndChild();
        }
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
