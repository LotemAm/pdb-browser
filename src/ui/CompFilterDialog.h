#pragma once
#include "pdb/SymbolNode.h"

#include <atomic>
#include <map>
#include <string>
#include <vector>

struct AppState;

// Tree node for grouping compilands by path segments.
struct CompTreeNode {
    std::string segment;
    std::map<std::string, CompTreeNode> children;    // sub-directories (sorted)
    std::vector<SymbolId> leafIds;                   // compilands at this level
};

class CompFilterDialog {
public:
    void render(AppState* state);

    // True while background thread is resolving compiland IDs.
    [[nodiscard]] bool isResolving() const { return m_resolving.load(); }

private:
    void buildTree(AppState* state);
    void renderTreeNode(CompTreeNode& node, AppState* state);
    void startResolve(AppState* state);

    CompTreeNode m_root;
    const void*  m_cachedIndex{nullptr};

    // Background compiland resolution state
    std::atomic<bool> m_resolving{false};
    std::atomic<int>  m_resolveProgress{0};
    int               m_resolveTotal{0};
    bool              m_resolved{false};     // true once resolution completed for current index
    const void*       m_resolvedIndex{nullptr};
};
