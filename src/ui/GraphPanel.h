#pragma once

#include <imgui-node-editor/imgui_node_editor.h>

#include "pdb/SymbolNode.h"

namespace ed = ax::NodeEditor;

struct AppState;
class PdbIndex;

class GraphPanel {
public:
    GraphPanel();
    ~GraphPanel();

    void render(AppState* state);
    void simpleRender(AppState* state);

private:
	void destroyEditor();
    void buildTypeGraph(SymbolId rootId, const PdbIndex* index, int maxHops = 2);

    struct GraphNode {
        SymbolId    symbolId;
        std::string label;
        size_t         hop{ 0 };     // BFS distance from root
    };

    struct GraphEdge {
        size_t fromNode;
        size_t toNode;
    };

    SymbolId                m_graphRoot = INVALID_SYMBOL_ID;
    SymbolId                m_pendingRoot = INVALID_SYMBOL_ID;
    SymbolId                m_lastSelected = INVALID_SYMBOL_ID;
    bool                    m_selFromGraph = false;
    bool                    m_lastPrettify = true;
    std::vector<GraphNode>  m_nodes;
    std::vector<GraphEdge>  m_edges;
    ed::EditorContext*      m_editorCtx = nullptr;
    bool                    m_needsLayout = false;
    int                     m_navCountdown = 0;
};
