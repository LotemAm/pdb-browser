#include "GraphPanel.h"
#include "app/AppState.h"
#include "pdb/PdbIndex.h"
#include "pdb/Prettify.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ── helpers ──────────────────────────────────────────────────────────────────

static [[nodiscard]] ed::NodeId nodeId(size_t idx)      { return ed::NodeId(idx + 1); }
static [[nodiscard]] ed::PinId  inputPinId(size_t idx)  { return ed::PinId(idx * 2 + 1); }
static [[nodiscard]] ed::PinId  outputPinId(size_t idx) { return ed::PinId(idx * 2 + 2); }
static [[nodiscard]] ed::LinkId linkId(size_t idx)      { return ed::LinkId(idx + 1); }

// ── GraphPanel ───────────────────────────────────────────────────────────────

GraphPanel::GraphPanel()
{
    ed::Config config;
    config.SettingsFile = nullptr;
    config.CanvasSizeMode = ed::CanvasSizeMode::CenterOnly;
    m_editorCtx = ed::CreateEditor(&config);
}

GraphPanel::~GraphPanel()
{
    destroyEditor();
}

void GraphPanel::destroyEditor()
{
    if (m_editorCtx) {
        ed::DestroyEditor(m_editorCtx);
        m_editorCtx = nullptr;
    }
}

// Build a simple N-hop neighbourhood graph around the selected UDT.
// Only includes named types (UDT / Enum / Typedef) — skips base types,
// pointers, arrays, and function types to keep the graph clean.
void GraphPanel::buildTypeGraph(SymbolId rootId, const PdbIndex* index, int maxHops)
{
    m_nodes.clear();
    m_edges.clear();
    m_graphRoot = rootId;
    if (!index) return;

    const SymbolNode* rootSym = index->getSymbol(rootId);
    if (!rootSym) return;

    // Only build type graphs for UDTs
    if (rootSym->kind != SymbolKind::UDT) return;

    bool prettify = m_lastPrettify;

    std::unordered_set<SymbolId> visited;
    std::unordered_map<SymbolId, size_t> symToNode;

    auto addNode = [&](SymbolId id, size_t hop) -> size_t {
        if (symToNode.contains(id)) return symToNode[id];
        const SymbolNode* sym = index->getSymbol(id);
        if (!sym || sym->name.empty()) return INVALID_SYMBOL_ID;

        auto nid = m_nodes.size();
        m_nodes.emplace_back(id, std::string(displayName(*sym, prettify)), hop);
        symToNode[id] = nid;
        return nid;
    };

    auto addEdgeIfNew = [&](auto from, auto to) {
        if (from < 0 || to < 0 || from == to) return;
        for (const auto& e : m_edges)
            if (e.fromNode == from && e.toNode == to) return;
        m_edges.push_back({ from, to });
    };

    // BFS
    struct Work { SymbolId id; int hop; };
    std::vector<Work> queue{ {rootId, 0} };

    while (!queue.empty()) {
        auto [id, hop] = queue.back();
        queue.pop_back();
        if (!visited.insert(id).second) continue;

        auto fromNode = addNode(id, hop);
        if (fromNode < 0) continue;

        const SymbolNode* sym = index->getSymbol(id);
        if (!sym || hop >= maxHops) continue;

        const auto* udtPtr = std::get_if<UdtData>(&sym->kindData);
        if (!udtPtr) continue;

        // Base classes → edges point from child to base
        for (SymbolId baseId : udtPtr->baseClasses) {
            const SymbolNode* baseSym = index->getSymbol(baseId);
            if (!baseSym || baseSym->name.empty()) continue;
            using enum SymbolKind;
            if (baseSym->kind != UDT && baseSym->kind != Enum && baseSym->kind != Typedef)
                continue;

            auto toNode = addNode(baseId, hop + 1);
            addEdgeIfNew(fromNode, toNode);
            if (!visited.contains(baseId))
                queue.push_back({ baseId, hop + 1 });
        }

        // Member types → only named types (UDT, Enum, Typedef)
        for (const MemberInfo& m : udtPtr->members) {
            if (m.typeId == INVALID_SYMBOL_ID) continue;
            const SymbolNode* mType = index->getSymbol(m.typeId);
            if (!mType || mType->name.empty()) continue;
            using enum SymbolKind;
            if (mType->kind != UDT && mType->kind != Enum && mType->kind != Typedef)
                continue;

            auto toNode = addNode(m.typeId, hop + 1);
            addEdgeIfNew(fromNode, toNode);
            if (!visited.contains(m.typeId))
                queue.push_back({ m.typeId, hop + 1 });
        }
    }

    // Recreate editor context for clean positioning state
    //destroyEditor();

    m_needsLayout = true;
    m_navCountdown = 2; // NavigateToContent after 2 frames so node sizes are known
}

void GraphPanel::render(AppState* state)
{
    ImGui::Begin("Graph");

    if (!state->activeIndex) {
        ImGui::TextDisabled("No PDB loaded.");
        ImGui::End();
        return;
    }

    PdbIndex* index = state->activeIndex.get();

    // ── Detect external selection changes ────────────────────────────────────
    // Graph only rebuilds when a LOADED UDT is selected from outside the graph.
    // Clicking a node within the graph updates the inspector without rebuilding.
    {
        bool externalChange = false;

        if (state->selectedSymbol != m_lastSelected) {
            m_lastSelected = state->selectedSymbol;
            if (!m_selFromGraph)
                externalChange = true;
        }
        m_selFromGraph = false; // always consume

        if (externalChange && state->selectedSymbol != INVALID_SYMBOL_ID) {
            const SymbolNode* sym = index->getSymbol(state->selectedSymbol);
            if (sym && sym->kind == SymbolKind::UDT)
                m_pendingRoot = state->selectedSymbol;
        }
    }

    // Rebuild graph when prettify toggle changes
    if (m_lastPrettify != state->prettifyNames && m_graphRoot != INVALID_SYMBOL_ID) {
        m_lastPrettify = state->prettifyNames;
        const SymbolNode* sym = index->getSymbol(m_graphRoot);
        if (sym && sym->childrenLoaded)
            buildTypeGraph(m_graphRoot, index);
    }
    m_lastPrettify = state->prettifyNames;

    // Build/rebuild when the pending root is a loaded UDT
    if (m_pendingRoot != INVALID_SYMBOL_ID && m_pendingRoot != m_graphRoot) {
        const SymbolNode* sym = index->getSymbol(m_pendingRoot);
        if (sym && sym->childrenLoaded) {
            buildTypeGraph(m_pendingRoot, index);
            m_pendingRoot = INVALID_SYMBOL_ID;
        }
    }

    // Also handle: pending root data just finished loading
    if (m_pendingRoot != INVALID_SYMBOL_ID && m_pendingRoot == m_graphRoot
        && m_nodes.empty())
    {
        const SymbolNode* sym = index->getSymbol(m_pendingRoot);
        if (sym && sym->childrenLoaded) {
            buildTypeGraph(m_pendingRoot, index);
            m_pendingRoot = INVALID_SYMBOL_ID;
        }
    }

    // ── Empty state messages ─────────────────────────────────────────────────
    if (m_nodes.empty()) {
        if (m_pendingRoot != INVALID_SYMBOL_ID) {
            ImGui::TextDisabled("Loading type data...");
        } else {
            ImGui::TextDisabled("Select a loaded UDT to view its type graph.");
        }
        ImGui::End();
        return;
    }

    //// ── Create editor if needed ──────────────────────────────────────────────
    //if (!m_editorCtx) {
    //    ed::Config config;
    //    config.SettingsFile = nullptr;
    //    m_editorCtx = ed::CreateEditor(&config);
    //}

    ed::SetCurrentEditor(m_editorCtx);
    ed::Begin("GraphT");

    // ── Draw nodes ───────────────────────────────────────────────────────────
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const GraphNode& gn = m_nodes[i];
        bool isRoot     = (gn.symbolId == m_graphRoot);
        bool isSelected = (gn.symbolId == state->selectedSymbol);

        // Highlight root and selected nodes with distinct colors
        if (isRoot) {
            ed::PushStyleColor(ed::StyleColor_NodeBg,     ImVec4(0.14f, 0.30f, 0.50f, 1.0f));
            ed::PushStyleColor(ed::StyleColor_NodeBorder,  ImVec4(0.30f, 0.60f, 0.90f, 1.0f));
        } else if (isSelected) {
            ed::PushStyleColor(ed::StyleColor_NodeBg,     ImVec4(0.25f, 0.40f, 0.25f, 1.0f));
            ed::PushStyleColor(ed::StyleColor_NodeBorder,  ImVec4(0.40f, 0.70f, 0.40f, 1.0f));
        }

        ed::BeginNode(nodeId(i));

        ed::BeginPin(inputPinId(i), ed::PinKind::Input);
        ImGui::TextUnformatted(">");
        ed::EndPin();

        ImGui::SameLine();
        ImGui::TextUnformatted(gn.label.c_str());
        ImGui::SameLine();

        ed::BeginPin(outputPinId(i), ed::PinKind::Output);
        ImGui::TextUnformatted(">");
        ed::EndPin();

        ed::EndNode();

        if (isRoot || isSelected)
            ed::PopStyleColor(2);
    }

    // ── Draw links ───────────────────────────────────────────────────────────
    for (size_t i = 0; i < m_edges.size(); ++i) {
        const GraphEdge& ge = m_edges[i];
        ed::Link(linkId(i), outputPinId(ge.fromNode), inputPinId(ge.toNode));
    }

    // ── Lay out nodes in columns by BFS hop distance ─────────────────────────
    if (m_needsLayout) {
        size_t maxHop = 0;
        for (const auto& gn : m_nodes)
            maxHop = std::max(maxHop, gn.hop);

        // Group node indices by hop
        std::vector<std::vector<size_t>> columns(maxHop + 1);
        for (size_t i = 0; i < m_nodes.size(); ++i)
            columns[m_nodes[i].hop].push_back(i);

        constexpr float kColSpacing = 300.f;
        constexpr float kRowSpacing = 80.f;

        for (int col = 0; col <= maxHop; ++col) {
            const auto& idxs = columns[col];
            float totalHeight = static_cast<float>(idxs.size() - 1) * kRowSpacing;
            float startY = -totalHeight * 0.5f;

            for (size_t r = 0; r < idxs.size(); ++r) {
                float x = static_cast<float>(col) * kColSpacing;
                float y = startY + static_cast<float>(r) * kRowSpacing;
                ed::SetNodePosition(nodeId(idxs[r]), ImVec2(x, y));
            }
        }

        m_needsLayout = false;
        m_navCountdown = 5; // Give it 2 frames to settle
    }

    // ── Handle node click — update inspector, keep graph stable ──────────────
    if (ed::HasSelectionChanged()) {
        std::vector<ed::NodeId> selected(ed::GetSelectedObjectCount());
        int count = ed::GetSelectedNodes(selected.data(), static_cast<int>(selected.size()));
        if (count > 0) {
            auto idx = selected[0].Get() - 1;
            if (idx >= 0 && idx < m_nodes.size()) {
                m_selFromGraph = true;
                state->selectSymbol(m_nodes[idx].symbolId);
            }
        }
    }

    ed::End();

    // ── Auto-zoom to fit all content (delayed for node size calculation) ─────
    if (m_navCountdown > 0) {
        --m_navCountdown;
        if (m_navCountdown == 0)
            ed::NavigateToContent(0.0f);
    }

    ed::SetCurrentEditor(nullptr);

    ImGui::End();
}

void GraphPanel::simpleRender([[maybe_unused]] AppState* state)
{
    int uniqueId = 1;
    ImGui::Begin("Graph");

        ed::SetCurrentEditor(m_editorCtx);

        ed::Begin("Graph");
            ed::BeginNode(uniqueId++);
                ImGui::Text("Node A");
                ed::BeginPin(uniqueId++, ed::PinKind::Input);
                    ImGui::Text("-> In");
                ed::EndPin();
                ImGui::SameLine();
                ed::BeginPin(uniqueId++, ed::PinKind::Output);
                    ImGui::Text("Out ->");
                ed::EndPin();
            ed::EndNode();
        ed::End();

    ImGui::End();
}
