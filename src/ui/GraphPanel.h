#pragma once

#include <imnodes.h>

#include "pdb/SymbolNode.h"

struct AppState;
class PdbIndex;

// ── Relationship categories (used for coloring and filtering) ───────────────
enum class EdgeCategory : uint8_t {
    Inheritance,     // base class / derived class
    MemberType,      // UDT member → its type
    MemberFunction,  // UDT → its method
    ReturnType,      // function → return type
    FriendClass,     // UDT → friend
    CompChild,       // compiland → child symbol
    COUNT
};

enum class NodeCategory : uint8_t {
    Root,            // the selected symbol
    UDT,
    Enum,
    Function,
    Typedef,
    Data,
    Compiland,
    Member,          // UDT data member (shown inline-style)
    COUNT
};

class GraphPanel {
public:
    GraphPanel();
    ~GraphPanel();

    void render(AppState* state);

private:
    void buildTypeGraph(SymbolId rootId, const PdbIndex* index);
    void renderFilterMenu();
    void renderLegend();

    struct GraphNode {
        SymbolId     symbolId;
        std::string  label;
        std::string  detail;       // extra info line (size, type, etc.)
        int          hop{ 0 };     // negative = ancestor, positive = descendant/related
        NodeCategory category{ NodeCategory::UDT };
    };

    struct GraphEdge {
        size_t       fromNode;
        size_t       toNode;
        EdgeCategory category{ EdgeCategory::MemberType };
    };

    // ── Graph data ──────────────────────────────────────────────────────────
    SymbolId                m_graphRoot    = INVALID_SYMBOL_ID;
    SymbolId                m_pendingRoot  = INVALID_SYMBOL_ID;
    SymbolId                m_lastSelected = INVALID_SYMBOL_ID;
    bool                    m_selFromGraph = false;
    bool                    m_lastPrettify = true;
    std::vector<GraphNode>  m_nodes;
    std::vector<GraphEdge>  m_edges;
    ImNodesEditorContext*   m_editorCtx    = nullptr;
    bool                    m_needsLayout  = false;
    int                     m_navCountdown = 0;
    int                     m_pendingPanNode = -1;

    // ── Filter toggles (what to show) ───────────────────────────────────────
    bool m_showInheritance    = true;
    bool m_showMembers        = true;
    bool m_showMemberFunctions= true;
    bool m_showReturnType     = true;
    bool m_showFriends        = true;
    bool m_showCompChildren   = true;
    bool m_legendOpen         = true;
    bool m_filtersOpen        = true;
    bool m_filtersChanged     = false;

    // ── Tracks which categories are used in current graph ───────────────────
    bool m_usedNodeCats[static_cast<int>(NodeCategory::COUNT)] {};
    bool m_usedEdgeCats[static_cast<int>(EdgeCategory::COUNT)] {};
    SymbolKind m_rootKind = SymbolKind::Unknown;
};
