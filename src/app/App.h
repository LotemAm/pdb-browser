#pragma once
#include <SDL3/SDL.h>
#include <memory>
#include <string>

#include "app/AppState.h"
#include "pdb/SymbolNode.h"
#include "ui/BrowserPanel.h"
#include "ui/CompFilterDialog.h"
#include "ui/GraphPanel.h"
#include "ui/InspectorPanel.h"
#include "ui/ExportDialog.h"
#include "ui/SearchDialog.h"

struct SDL_Window;

class App {
public:
    App();
    ~App();

    // Returns false if initialization failed.
    bool init();

    // Runs the main loop until the user closes the window.
    void run();

    void onSymbolClicked(SymbolId symbolId);

private:
    void processEvents(bool& running);
    void update();          // poll load state, kick background thread, etc.
    void render();          // ImGui frame
    void renderMenuBar();

    void openPdb(const std::string& path);
    void showOpenDialog();

    SDL_Window*    m_window{nullptr};
    SDL_GLContext  m_glContext{nullptr};

    std::unique_ptr<AppState> m_state;

	BrowserPanel     m_browserPanel;
	GraphPanel       m_graphPanel;
	InspectorPanel   m_inspectorPanel;
	SearchDialog     m_searchDialog;
	ExportDialog     m_exportDialog;
	CompFilterDialog m_compFilterDialog;
};
