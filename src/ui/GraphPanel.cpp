#include "GraphPanel.h"
#include "app/AppState.h"
#include "pdb/PdbIndex.h"
#include "pdb/Prettify.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ── ID encoding ─────────────────────────────────────────────────────────────
static int nodeId(size_t idx)       { return static_cast<int>(idx + 1); }
static int inputAttrId(size_t idx)  { return static_cast<int>(idx * 2 + 1); }
static int outputAttrId(size_t idx) { return static_cast<int>(idx * 2 + 2); }
static int linkId(size_t idx)       { return static_cast<int>(idx + 1); }

// ── Node max width ──────────────────────────────────────────────────────────
static constexpr float kNodeMaxWidth = 280.f;

// ── Color palette ───────────────────────────────────────────────────────────

static constexpr ImU32 col(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}

static ImU32 nodeTitleColor(NodeCategory cat) {
    switch (cat) {
    case NodeCategory::Root:       return col(36, 77, 128);
    case NodeCategory::UDT:        return col(60, 100, 140);
    case NodeCategory::Enum:       return col(140, 90, 50);
    case NodeCategory::Function:   return col(80, 50, 130);
    case NodeCategory::Typedef:    return col(50, 120, 100);
    case NodeCategory::Data:       return col(100, 100, 60);
    case NodeCategory::Compiland:  return col(90, 70, 110);
    case NodeCategory::Member:     return col(70, 85, 70);
    default:                       return col(80, 80, 80);
    }
}

static const char* nodeCategoryLabel(NodeCategory cat) {
    switch (cat) {
    case NodeCategory::Root:       return "Root (selected)";
    case NodeCategory::UDT:        return "Class / Struct";
    case NodeCategory::Enum:       return "Enum";
    case NodeCategory::Function:   return "Function";
    case NodeCategory::Typedef:    return "Typedef";
    case NodeCategory::Data:       return "Data";
    case NodeCategory::Compiland:  return "Compiland";
    case NodeCategory::Member:     return "Member";
    default:                       return "Unknown";
    }
}

static ImU32 edgeLinkColor(EdgeCategory cat) {
    switch (cat) {
    case EdgeCategory::Inheritance:    return col(100, 180, 255);
    case EdgeCategory::MemberType:     return col(120, 200, 120);
    case EdgeCategory::MemberFunction: return col(180, 130, 220);
    case EdgeCategory::ReturnType:     return col(220, 180, 80);
    case EdgeCategory::FriendClass:    return col(180, 180, 100);
    case EdgeCategory::CompChild:      return col(150, 150, 180);
    default:                           return col(150, 150, 150);
    }
}

static const char* edgeCategoryLabel(EdgeCategory cat) {
    switch (cat) {
    case EdgeCategory::Inheritance:    return "Inheritance";
    case EdgeCategory::MemberType:     return "Member type";
    case EdgeCategory::MemberFunction: return "Method";
    case EdgeCategory::ReturnType:     return "Return type";
    case EdgeCategory::FriendClass:    return "Friend";
    case EdgeCategory::CompChild:      return "Compiland child";
    default:                           return "Unknown";
    }
}

static NodeCategory kindToNodeCategory(SymbolKind kind) {
    switch (kind) {
    case SymbolKind::UDT:        return NodeCategory::UDT;
    case SymbolKind::Enum:       return NodeCategory::Enum;
    case SymbolKind::Function:   return NodeCategory::Function;
    case SymbolKind::Typedef:    return NodeCategory::Typedef;
    case SymbolKind::Data:       return NodeCategory::Data;
    case SymbolKind::Compiland:  return NodeCategory::Compiland;
    default:                     return NodeCategory::Data;
    }
}


// ── GraphPanel ──────────────────────────────────────────────────────────────

GraphPanel::GraphPanel()
{
    m_editorCtx = ImNodes::EditorContextCreate();
}

GraphPanel::~GraphPanel()
{
    if (m_editorCtx) {
        ImNodes::EditorContextFree(m_editorCtx);
        m_editorCtx = nullptr;
    }
}

void GraphPanel::buildTypeGraph(SymbolId rootId, const PdbIndex* index)
{
    m_nodes.clear();
    m_edges.clear();
    m_graphRoot = rootId;
    std::memset(m_usedNodeCats, 0, sizeof(m_usedNodeCats));
    std::memset(m_usedEdgeCats, 0, sizeof(m_usedEdgeCats));
    m_rootKind = SymbolKind::Unknown;

    if (m_editorCtx) {
        ImNodes::EditorContextFree(m_editorCtx);
        m_editorCtx = ImNodes::EditorContextCreate();
    }

    if (!index) return;

    const SymbolNode* rootSym = index->getSymbol(rootId);
    if (!rootSym) return;

    m_rootKind = rootSym->kind;
    bool prettify = m_lastPrettify;

    std::unordered_map<SymbolId, size_t> symToNode;

    auto markNode = [&](NodeCategory cat) { m_usedNodeCats[static_cast<int>(cat)] = true; };
    auto markEdge = [&](EdgeCategory cat) { m_usedEdgeCats[static_cast<int>(cat)] = true; };

    // Build detail string for a function symbol
    auto buildFuncDetail = [](const SymbolNode* sym) -> std::string {
        const auto* fnData = std::get_if<FunctionData>(&sym->kindData);
        if (!fnData) return {};
        std::string det;
        if (sym->rva != 0)
            det = std::format("RVA: 0x{:X}", sym->rva);
        std::string flags;
        if (fnData->isVirtual)
            flags += fnData->isPure ? "pure virtual" : "virtual";
        if (fnData->isStatic) {
            if (!flags.empty()) flags += ", ";
            flags += "static";
        }
        if (fnData->isInline) {
            if (!flags.empty()) flags += ", ";
            flags += "inline";
        }
        if (!flags.empty()) {
            if (!det.empty()) det += '\n';
            det += flags;
        }
        return det;
    };

    auto addNode = [&](SymbolId id, int hop, NodeCategory cat,
                       std::string detail = {}, std::string labelOverride = {}) -> size_t {
        if (auto it = symToNode.find(id); it != symToNode.end())
            return it->second;
        const SymbolNode* sym = index->getSymbol(id);
        if (!sym || sym->name.empty()) return INVALID_SYMBOL_ID;

        auto nid = m_nodes.size();
        GraphNode gn;
        gn.symbolId = id;
        gn.label    = labelOverride.empty() ? std::string(displayName(*sym, prettify)) : std::move(labelOverride);
        gn.detail   = std::move(detail);
        gn.hop      = hop;
        gn.category = cat;
        m_nodes.push_back(std::move(gn));
        symToNode[id] = nid;
        markNode(cat);
        return nid;
    };

    // Member pseudo-node (each member is its own node, not deduped by typeId)
    auto addMemberNode = [&](const MemberInfo& mi, bool prettify_, int hop) -> size_t {
        std::string label = mi.name;
        std::string detail;
        auto typeName = std::string(prettify_ ? displayTypeName(mi.typeName, mi.prettyTypeName, true)
                                              : std::string_view(mi.typeName));
        if (!typeName.empty())
            detail = typeName;
        if (mi.sizeBytes > 0)
            detail += std::format("  ({} B)", mi.sizeBytes);

        auto nid = m_nodes.size();
        GraphNode gn;
        gn.symbolId = mi.typeId;
        gn.label    = std::move(label);
        gn.detail   = std::move(detail);
        gn.hop      = hop;
        gn.category = NodeCategory::Member;
        m_nodes.push_back(std::move(gn));
        markNode(NodeCategory::Member);
        return nid;
    };

    auto addEdge = [&](size_t from, size_t to, EdgeCategory cat) {
        if (from == INVALID_SYMBOL_ID || to == INVALID_SYMBOL_ID || from == to) return;
        m_edges.push_back({ from, to, cat });
        markEdge(cat);
    };

    auto isEdgeEnabled = [&](EdgeCategory cat) -> bool {
        switch (cat) {
        case EdgeCategory::Inheritance:    return m_showInheritance;
        case EdgeCategory::MemberType:     return m_showMembers;
        case EdgeCategory::MemberFunction: return m_showMemberFunctions;
        case EdgeCategory::ReturnType:     return m_showReturnType;
        case EdgeCategory::FriendClass:    return m_showFriends;
        case EdgeCategory::CompChild:      return m_showCompChildren;
        default: return true;
        }
    };

    // ── Build root node ─────────────────────────────────────────────────────
    std::string rootDetail;
    if (rootSym->sizeBytes > 0)
        rootDetail = std::format("{} bytes", rootSym->sizeBytes);

    // For functions, clean the label
    std::string rootLabel;
    if (rootSym->kind == SymbolKind::Function)
        rootLabel = stripFunctionQualifiers(displayName(*rootSym, prettify));

    size_t rootNode = addNode(rootId, 0, NodeCategory::Root, rootDetail, rootLabel);
    if (rootNode == INVALID_SYMBOL_ID) return;

    // ── UDT root ────────────────────────────────────────────────────────────
    if (const auto* udtPtr = std::get_if<UdtData>(&rootSym->kindData)) {

        // Base classes (ancestors) — placed to the LEFT of root (negative hops)
        if (isEdgeEnabled(EdgeCategory::Inheritance)) {
            for (SymbolId baseId : udtPtr->baseClasses) {
                const SymbolNode* baseSym = index->getSymbol(baseId);
                if (!baseSym || baseSym->name.empty()) continue;
                std::string det;
                if (baseSym->sizeBytes > 0) det = std::format("{} bytes", baseSym->sizeBytes);
                auto baseNode = addNode(baseId, -1, NodeCategory::UDT, det);
                addEdge(baseNode, rootNode, EdgeCategory::Inheritance);

                // Grandparents
                if (baseSym->childrenLoaded) {
                    if (const auto* baseUdt = std::get_if<UdtData>(&baseSym->kindData)) {
                        for (SymbolId gpId : baseUdt->baseClasses) {
                            const SymbolNode* gpSym = index->getSymbol(gpId);
                            if (!gpSym || gpSym->name.empty()) continue;
                            std::string gpDet;
                            if (gpSym->sizeBytes > 0) gpDet = std::format("{} bytes", gpSym->sizeBytes);
                            auto gpNode = addNode(gpId, -2, NodeCategory::UDT, gpDet);
                            addEdge(gpNode, baseNode, EdgeCategory::Inheritance);
                        }
                    }
                }
            }

            // Derived classes (from UdtData, populated during load)
            for (SymbolId derivedId : udtPtr->derivedClasses) {
                const SymbolNode* derivedSym = index->getSymbol(derivedId);
                if (!derivedSym || derivedSym->name.empty()) continue;
                std::string det;
                if (derivedSym->sizeBytes > 0) det = std::format("{} bytes", derivedSym->sizeBytes);
                auto derivedNode = addNode(derivedId, 1, NodeCategory::UDT, det);
                addEdge(rootNode, derivedNode, EdgeCategory::Inheritance);
            }
        }

        // Members
        if (isEdgeEnabled(EdgeCategory::MemberType)) {
            for (const MemberInfo& mi : udtPtr->members) {
                auto memberNode = addMemberNode(mi, prettify, 1);
                addEdge(rootNode, memberNode, EdgeCategory::MemberType);

                if (mi.typeId != INVALID_SYMBOL_ID) {
                    const SymbolNode* mType = index->getSymbol(mi.typeId);
                    if (mType && !mType->name.empty()) {
                        using enum SymbolKind;
                        if (mType->kind == UDT || mType->kind == Enum || mType->kind == Typedef) {
                            std::string tDet;
                            if (mType->sizeBytes > 0) tDet = std::format("{} bytes", mType->sizeBytes);
                            auto typeNode = addNode(mi.typeId, 2, kindToNodeCategory(mType->kind), tDet);
                            addEdge(memberNode, typeNode, EdgeCategory::MemberType);
                        }
                    }
                }
            }
        }

        // Member functions
        if (isEdgeEnabled(EdgeCategory::MemberFunction)) {
            for (SymbolId childId : rootSym->children) {
                const SymbolNode* childSym = index->getSymbol(childId);
                if (!childSym || childSym->name.empty()) continue;
                if (childSym->kind == SymbolKind::Function) {
                    auto label = stripFunctionQualifiers(displayName(*childSym, prettify));
                    auto funcNode = addNode(childId, 1, NodeCategory::Function, buildFuncDetail(childSym), label);
                    addEdge(rootNode, funcNode, EdgeCategory::MemberFunction);
                }
            }
        }

        // Friends
        if (isEdgeEnabled(EdgeCategory::FriendClass)) {
            for (SymbolId friendId : udtPtr->friends) {
                const SymbolNode* friendSym = index->getSymbol(friendId);
                if (!friendSym || friendSym->name.empty()) continue;
                auto friendNode = addNode(friendId, 1, kindToNodeCategory(friendSym->kind));
                addEdge(rootNode, friendNode, EdgeCategory::FriendClass);
            }
        }
    }

    // ── Function root ───────────────────────────────────────────────────────
    else if (std::holds_alternative<FunctionData>(rootSym->kindData)) {

        m_nodes[rootNode].detail = buildFuncDetail(rootSym);

        // Parent class — to the left
        if (isEdgeEnabled(EdgeCategory::Inheritance) && rootSym->parentId != INVALID_SYMBOL_ID) {
            const SymbolNode* parentSym = index->getSymbol(rootSym->parentId);
            if (parentSym && !parentSym->name.empty()) {
                std::string det;
                if (parentSym->sizeBytes > 0) det = std::format("{} bytes", parentSym->sizeBytes);
                auto parentNode = addNode(rootSym->parentId, -1, NodeCategory::UDT, det);
                addEdge(parentNode, rootNode, EdgeCategory::Inheritance);
            }
        }

        // Return type
        if (isEdgeEnabled(EdgeCategory::ReturnType) && rootSym->typeId != INVALID_SYMBOL_ID) {
            const SymbolNode* retSym = index->getSymbol(rootSym->typeId);
            if (retSym && !retSym->name.empty()) {
                std::string det;
                if (retSym->sizeBytes > 0) det = std::format("{} bytes", retSym->sizeBytes);
                auto retNode = addNode(rootSym->typeId, 1, kindToNodeCategory(retSym->kind), det);
                addEdge(rootNode, retNode, EdgeCategory::ReturnType);
            }
        }

        // Template argument types
        for (const auto& targ : rootSym->templateArgs) {
            if (targ.typeId != INVALID_SYMBOL_ID) {
                const SymbolNode* targSym = index->getSymbol(targ.typeId);
                if (targSym && !targSym->name.empty()) {
                    auto targNode = addNode(targ.typeId, 1, kindToNodeCategory(targSym->kind));
                    addEdge(rootNode, targNode, EdgeCategory::MemberType);
                }
            }
        }
    }

    // ── Compiland root ──────────────────────────────────────────────────────
    else if (std::holds_alternative<CompilandData>(rootSym->kindData)) {
        if (isEdgeEnabled(EdgeCategory::CompChild)) {
            for (SymbolId childId : rootSym->children) {
                const SymbolNode* childSym = index->getSymbol(childId);
                if (!childSym || childSym->name.empty()) continue;
                auto childCat = kindToNodeCategory(childSym->kind);
                std::string label;
                std::string det;
                if (childSym->kind == SymbolKind::Function) {
                    label = stripFunctionQualifiers(displayName(*childSym, prettify));
                    det = buildFuncDetail(childSym);
                }
                auto childNode = addNode(childId, 1, childCat, det, label);
                addEdge(rootNode, childNode, EdgeCategory::CompChild);
            }
        }
    }

    // ── Enum / Typedef / Data root ──────────────────────────────────────────
    else {
        if (rootSym->typeId != INVALID_SYMBOL_ID) {
            const SymbolNode* typeSym = index->getSymbol(rootSym->typeId);
            if (typeSym && !typeSym->name.empty()) {
                std::string det;
                if (typeSym->sizeBytes > 0) det = std::format("{} bytes", typeSym->sizeBytes);
                auto typeNode = addNode(rootSym->typeId, 1, kindToNodeCategory(typeSym->kind), det);
                addEdge(rootNode, typeNode, EdgeCategory::ReturnType);
            }
        }
        if (rootSym->parentId != INVALID_SYMBOL_ID) {
            const SymbolNode* parentSym = index->getSymbol(rootSym->parentId);
            if (parentSym && !parentSym->name.empty()) {
                auto parentNode = addNode(rootSym->parentId, -1, kindToNodeCategory(parentSym->kind));
                addEdge(parentNode, rootNode, EdgeCategory::Inheritance);
            }
        }
    }

    m_needsLayout = true;
    m_navCountdown = 5;
}

// ── Filter menu (collapsible, only relevant items) ──────────────────────────

void GraphPanel::renderFilterMenu()
{
    ImGui::SetNextItemOpen(m_filtersOpen);
    m_filtersOpen = ImGui::CollapsingHeader("Filters");
    if (!m_filtersOpen) return;

    ImGui::Indent(8.f);

    using enum SymbolKind;

    // Show only filters relevant to the current root kind
    if (m_rootKind == UDT) {
        m_filtersChanged |= ImGui::Checkbox("Inheritance",      &m_showInheritance);
        m_filtersChanged |= ImGui::Checkbox("Members",          &m_showMembers);
        m_filtersChanged |= ImGui::Checkbox("Methods",          &m_showMemberFunctions);
        m_filtersChanged |= ImGui::Checkbox("Friends",          &m_showFriends);
    } else if (m_rootKind == Function) {
        m_filtersChanged |= ImGui::Checkbox("Parent class",     &m_showInheritance);
        m_filtersChanged |= ImGui::Checkbox("Return type",      &m_showReturnType);
    } else if (m_rootKind == Compiland) {
        m_filtersChanged |= ImGui::Checkbox("Children",         &m_showCompChildren);
    } else {
        // Enum / Typedef / Data — show everything applicable
        m_filtersChanged |= ImGui::Checkbox("Related types",    &m_showReturnType);
        m_filtersChanged |= ImGui::Checkbox("Parent",           &m_showInheritance);
    }

    ImGui::Unindent(8.f);
}

// ── Color legend (collapsible, only used categories) ────────────────────────

void GraphPanel::renderLegend()
{
    ImGui::SetNextItemOpen(m_legendOpen);
    m_legendOpen = ImGui::CollapsingHeader("Legend");
    if (!m_legendOpen) return;

    ImGui::Indent(8.f);

    // Node colors — only show used categories
    bool anyNode = false;
    for (int i = 0; i < static_cast<int>(NodeCategory::COUNT); ++i)
        if (m_usedNodeCats[i]) { anyNode = true; break; }

    if (anyNode) {
        ImGui::TextUnformatted("Nodes:");
        ImGui::Indent(8.f);
        for (int i = 0; i < static_cast<int>(NodeCategory::COUNT); ++i) {
            if (!m_usedNodeCats[i]) continue;
            auto cat = static_cast<NodeCategory>(i);
            ImVec4 cv = ImGui::ColorConvertU32ToFloat4(nodeTitleColor(cat));
            ImGui::ColorButton(nodeCategoryLabel(cat), cv,
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                               ImVec2(12, 12));
            ImGui::SameLine();
            ImGui::TextUnformatted(nodeCategoryLabel(cat));
        }
        ImGui::Unindent(8.f);
    }

    // Edge colors — only show used categories
    bool anyEdge = false;
    for (int i = 0; i < static_cast<int>(EdgeCategory::COUNT); ++i)
        if (m_usedEdgeCats[i]) { anyEdge = true; break; }

    if (anyEdge) {
        ImGui::Spacing();
        ImGui::TextUnformatted("Links:");
        ImGui::Indent(8.f);
        for (int i = 0; i < static_cast<int>(EdgeCategory::COUNT); ++i) {
            if (!m_usedEdgeCats[i]) continue;
            auto cat = static_cast<EdgeCategory>(i);
            ImVec4 cv = ImGui::ColorConvertU32ToFloat4(edgeLinkColor(cat));
            ImGui::ColorButton(edgeCategoryLabel(cat), cv,
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                               ImVec2(12, 12));
            ImGui::SameLine();
            ImGui::TextUnformatted(edgeCategoryLabel(cat));
        }
        ImGui::Unindent(8.f);
    }

    ImGui::Unindent(8.f);
}

// ── Main render ─────────────────────────────────────────────────────────────

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
    {
        bool externalChange = false;

        if (state->selectedSymbol != m_lastSelected) {
            m_lastSelected = state->selectedSymbol;
            if (!m_selFromGraph)
                externalChange = true;
        }
        m_selFromGraph = false;

        if (externalChange && state->selectedSymbol != INVALID_SYMBOL_ID) {
            bool foundInGraph = false;
            for (size_t i = 0; i < m_nodes.size(); ++i) {
                if (m_nodes[i].symbolId == state->selectedSymbol) {
                    m_pendingPanNode = nodeId(i);
                    foundInGraph = true;
                    break;
                }
            }
            if (!foundInGraph)
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

    // Build/rebuild when the pending root's data has been loaded
    if (m_pendingRoot != INVALID_SYMBOL_ID && m_pendingRoot != m_graphRoot) {
        const SymbolNode* sym = index->getSymbol(m_pendingRoot);
        if (sym && sym->childrenLoaded) {
            buildTypeGraph(m_pendingRoot, index);
            m_pendingRoot = INVALID_SYMBOL_ID;
        }
    }

    if (m_pendingRoot != INVALID_SYMBOL_ID && m_pendingRoot == m_graphRoot
        && m_nodes.empty())
    {
        const SymbolNode* sym = index->getSymbol(m_pendingRoot);
        if (sym && sym->childrenLoaded) {
            buildTypeGraph(m_pendingRoot, index);
            m_pendingRoot = INVALID_SYMBOL_ID;
        }
    }

    // Rebuild when filters change
    if (m_filtersChanged && m_graphRoot != INVALID_SYMBOL_ID) {
        m_filtersChanged = false;
        const SymbolNode* sym = index->getSymbol(m_graphRoot);
        if (sym && sym->childrenLoaded)
            buildTypeGraph(m_graphRoot, index);
    }

    // ── Sidebar: filters + legend ───────────────────────────────────────────
    renderFilterMenu();
    renderLegend();
    ImGui::Separator();

    // ── Empty state ─────────────────────────────────────────────────────────
    if (m_nodes.empty()) {
        if (m_pendingRoot != INVALID_SYMBOL_ID) {
            ImGui::TextDisabled("Loading type data...");
        } else {
            ImGui::TextDisabled("Select a symbol to view its graph.");
        }
        ImGui::End();
        return;
    }

    // ── Render node editor ───────────────────────────────────────────────────
    ImNodes::EditorContextSet(m_editorCtx);
    ImNodes::BeginNodeEditor();

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const GraphNode& gn = m_nodes[i];

        ImU32 titleCol = nodeTitleColor(gn.category);
        int r = (titleCol >> 0) & 0xFF;
        int g = (titleCol >> 8) & 0xFF;
        int b = (titleCol >> 16) & 0xFF;

        ImNodes::PushColorStyle(ImNodesCol_TitleBar,         IM_COL32(r, g, b, 255));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered,  IM_COL32(std::min(r+20,255), std::min(g+20,255), std::min(b+20,255), 255));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(std::min(r+40,255), std::min(g+40,255), std::min(b+40,255), 255));

        ImNodes::BeginNode(nodeId(i));

        ImNodes::BeginNodeTitleBar();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kNodeMaxWidth);
        ImGui::TextWrapped("%s", gn.label.c_str());
        ImGui::PopTextWrapPos();
        ImNodes::EndNodeTitleBar();

        // Force a fixed content width so the node doesn't expand beyond it
        ImGui::Dummy(ImVec2(kNodeMaxWidth, 0.f));

        if (!gn.detail.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kNodeMaxWidth);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::TextWrapped("%s", gn.detail.c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        }

        ImNodes::BeginInputAttribute(inputAttrId(i));
        ImNodes::EndInputAttribute();

        ImNodes::BeginOutputAttribute(outputAttrId(i));
        ImNodes::EndOutputAttribute();

        ImNodes::EndNode();

        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    // Draw links with category colors
    for (size_t i = 0; i < m_edges.size(); ++i) {
        const GraphEdge& ge = m_edges[i];

        ImU32 linkCol = edgeLinkColor(ge.category);
        ImNodes::PushColorStyle(ImNodesCol_Link,         linkCol);
        ImNodes::PushColorStyle(ImNodesCol_LinkHovered,  linkCol);
        ImNodes::PushColorStyle(ImNodesCol_LinkSelected, linkCol);

        ImNodes::Link(linkId(i), outputAttrId(ge.fromNode), inputAttrId(ge.toNode));

        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    // Lay out nodes in columns by hop distance (negative = left, 0 = center, positive = right)
    if (m_needsLayout) {
        int minHop = 0, maxHop = 0;
        for (const auto& gn : m_nodes) {
            minHop = std::min(minHop, gn.hop);
            maxHop = std::max(maxHop, gn.hop);
        }

        int numCols = maxHop - minHop + 1;
        std::vector<std::vector<size_t>> columns(numCols);
        for (size_t i = 0; i < m_nodes.size(); ++i)
            columns[m_nodes[i].hop - minHop].push_back(i);

        constexpr float kColSpacing = 420.f;
        constexpr float kRowSpacing = 110.f;

        for (int c = 0; c < numCols; ++c) {
            const auto& idxs = columns[c];
            if (idxs.empty()) continue;
            float totalHeight = static_cast<float>(idxs.size() - 1) * kRowSpacing;
            float startY = -totalHeight * 0.5f;

            for (size_t r = 0; r < idxs.size(); ++r) {
                float x = static_cast<float>(c) * kColSpacing;
                float y = startY + static_cast<float>(r) * kRowSpacing;
                ImNodes::SetNodeEditorSpacePos(nodeId(idxs[r]), ImVec2(x, y));
            }
        }

        m_needsLayout = false;
        m_navCountdown = 5;
    }

    ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);

    ImNodes::EndNodeEditor();

    // ── Double-click a node → rebuild graph around that symbol ────────────────
    int hoveredNode = -1;
    if (ImNodes::IsNodeHovered(&hoveredNode) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        int idx = hoveredNode - 1;
        if (idx >= 0 && static_cast<size_t>(idx) < m_nodes.size()) {
            SymbolId symId = m_nodes[idx].symbolId;
            if (symId != INVALID_SYMBOL_ID) {
                m_selFromGraph = true;
                m_pendingRoot = symId;       // force graph rebuild
                m_graphRoot = INVALID_SYMBOL_ID; // ensure rebuild even if same symbol
                state->selectSymbol(symId);
            }
        }
    }

    // ── Pan to center a node in the viewport ────────────────────────────────
    auto centerOnNode = [](int nid) {
        ImVec2 nodePos  = ImNodes::GetNodeEditorSpacePos(nid);
        ImVec2 nodeDims = ImNodes::GetNodeDimensions(nid);
        ImVec2 viewSize = ImGui::GetWindowSize();
        ImVec2 center{
            nodePos.x + nodeDims.x * 0.5f - viewSize.x * 0.5f,
            nodePos.y + nodeDims.y * 0.5f - viewSize.y * 0.5f,
        };
        ImNodes::EditorContextResetPanning(ImVec2(-center.x, -center.y));
    };

    if (m_navCountdown > 0) {
        --m_navCountdown;
        if (m_navCountdown == 0 && !m_nodes.empty())
            centerOnNode(nodeId(0));
    } else if (m_pendingPanNode >= 0) {
        centerOnNode(m_pendingPanNode);
        m_pendingPanNode = -1;
    }

    ImGui::End();
}
