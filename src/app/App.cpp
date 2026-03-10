#include "App.h"
#include "AppState.h"

#include "pdb/PdbSession.h"
#include "pdb/PdbIndex.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#include <SDL3/SDL_opengl.h>

#include <print>
#include <thread>

// ── helpers ──────────────────────────────────────────────────────────────────

static void backgroundLoad(AppState* state, std::string path)
{
    state->activePdbSession = std::make_unique<PdbSession>();
    auto result = state->activePdbSession->load(path);

    std::lock_guard lock(state->loadMutex);
    if (result) {
        state->loadResult.index = std::move(*result);
        state->loadState.store(LoadState::Ready);
    } else {
        state->loadResult.errorMessage = std::move(result.error());
        state->loadState.store(LoadState::Failed);
    }
}

static void backgroundGetSymbolData(AppState* state, SymbolId id)
{
    auto result = state->activePdbSession->loadSymbolData(state->activeIndex, id);

    std::lock_guard lock(state->loadMutex);
    if (result) {
        state->loadResult.loadedSymbolId = id;
        state->loadState.store(LoadState::Ready);
    } else {
        state->loadResult.errorMessage = std::move(result.error());
        state->loadState.store(LoadState::Failed);
    }
}

static void SDLCALL onFileDialogResult(void* userdata, const char* const* filelist, int /*filter*/)
{
    auto* state = static_cast<AppState*>(userdata);
    if (!filelist || !*filelist) return;  // cancelled or error
    state->pendingPdbPath = *filelist;
}

void AppState::selectSymbol(SymbolId id)
{
    if (selectedSymbol != INVALID_SYMBOL_ID)
        navBack.push_back(selectedSymbol);
    navForward.clear();
    selectedSymbol = id;
    app->onSymbolClicked(id);
}

void AppState::navigateBack()
{
    if (navBack.empty()) return;
    SymbolId target = navBack.back();
    navBack.pop_back();
    if (selectedSymbol != INVALID_SYMBOL_ID)
        navForward.push_back(selectedSymbol);
    selectedSymbol = target;
    app->onSymbolClicked(target);
}

void AppState::navigateForward()
{
    if (navForward.empty()) return;
    SymbolId target = navForward.back();
    navForward.pop_back();
    if (selectedSymbol != INVALID_SYMBOL_ID)
        navBack.push_back(selectedSymbol);
    selectedSymbol = target;
    app->onSymbolClicked(target);
}

// ── App ──────────────────────────────────────────────────────────────────────

App::App()
    : m_state(std::make_unique<AppState>())
{
    m_state->app = this;
}

App::~App()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (m_glContext) SDL_GL_DestroyContext(m_glContext);
    if (m_window)    SDL_DestroyWindow(m_window);
    SDL_Quit();
}

bool App::init()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println(stderr, "SDL_Init error: {}", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    
    SDL_Rect displayBounds;
    SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &displayBounds);

    m_window = SDL_CreateWindow("PDB Browser", displayBounds.w / 2, displayBounds.h / 2,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!m_window) {
        std::println(stderr, "SDL_CreateWindow error: {}", SDL_GetError());
        return false;
    }

    m_glContext = SDL_GL_CreateContext(m_window);
    SDL_GL_MakeCurrent(m_window, m_glContext);
    SDL_GL_SetSwapInterval(1);

    // Detect display content scale for HiDPI / 4K support
    SDL_DisplayID displayId = SDL_GetDisplayForWindow(m_window);
    float dpiScale = SDL_GetDisplayContentScale(displayId);
    if (dpiScale <= 0.f) dpiScale = 1.f;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

#ifdef _DEBUG
    io.ConfigDebugIsDebuggerPresent = true;
#endif

    // Load a TTF font at DPI-scaled size for crisp text on HiDPI / 4K displays
    const float fontSize = 18.f * dpiScale;
    ImFont* font = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", fontSize);
    if (!font) {
        // Fallback: use built-in bitmap font with global scale
        io.Fonts->AddFontDefault();
        io.FontGlobalScale = dpiScale;
    }

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(dpiScale);

    ImGui_ImplSDL3_InitForOpenGL(m_window, m_glContext);
    ImGui_ImplOpenGL3_Init("#version 330");

    return true;
}

void App::run()
{
    bool running = true;
    while (running) {
        processEvents(running);
        update();
        render();
        SDL_GL_SwapWindow(m_window);
    }
}

void App::processEvents(bool& running)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT)
            running = false;
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
            event.window.windowID == SDL_GetWindowID(m_window))
            running = false;
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (event.button.button == SDL_BUTTON_X1)
                m_state->navigateBack();
            else if (event.button.button == SDL_BUTTON_X2)
                m_state->navigateForward();
        }
        if (event.type == SDL_EVENT_KEY_DOWN) {
            const bool ctrl  = (event.key.mod & SDL_KMOD_CTRL) != 0;
            const bool alt   = (event.key.mod & SDL_KMOD_ALT) != 0;
            if (ctrl && event.key.scancode == SDL_SCANCODE_O)
                m_state->requestOpen = true;
            if (ctrl && event.key.scancode == SDL_SCANCODE_F) {
                m_state->showSearch = !m_state->showSearch;
                if (m_state->showSearch) m_state->searchFocusInput = true;
            }
            if (ctrl && event.key.scancode == SDL_SCANCODE_E && m_state->activeIndex)
                m_state->showExportDialog = !m_state->showExportDialog;
            if (alt && event.key.scancode == SDL_SCANCODE_LEFT)
                m_state->navigateBack();
            if (alt && event.key.scancode == SDL_SCANCODE_RIGHT)
                m_state->navigateForward();
        }
    }
}

void App::update()
{
    // Swap in results once background work completes
    LoadState ls = m_state->loadState.load();
    if (ls == LoadState::Ready) {
        std::lock_guard lock(m_state->loadMutex);
        if (m_state->loadResult.index) {
            // Initial PDB load finished
            m_state->activeIndex = std::move(m_state->loadResult.index);
        }
        if (m_state->loadResult.loadedSymbolId != INVALID_SYMBOL_ID) {
            // Per-symbol data load finished — expose the selection to the UI
            m_state->selectedSymbol = m_state->loadResult.loadedSymbolId;
            m_state->loadResult.loadedSymbolId = INVALID_SYMBOL_ID;
        }
        m_state->loadState.store(LoadState::Idle);
    }

    // Handle File > Open flow
    if (m_state->requestOpen) {
        m_state->requestOpen = false;
        showOpenDialog();
    }
    if (!m_state->pendingPdbPath.empty() &&
        m_state->loadState.load() == LoadState::Idle)
    {
        openPdb(m_state->pendingPdbPath);
        m_state->pendingPdbPath.clear();
    }

    // Tick down export status message timer
    if (m_state->exportStatusTimer > 0.f) {
        m_state->exportStatusTimer -= ImGui::GetIO().DeltaTime;
        if (m_state->exportStatusTimer <= 0.f)
            m_state->exportStatusMessage.clear();
    }
}

void App::render()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Full-screen dockspace
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::Begin("##DockHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    renderMenuBar();

    ImGuiID dockId = ImGui::GetID("MainDock");

    // Build initial layout once — Browser top half, Inspector+Graph tabbed bottom half
    static bool s_layoutInitialized = false;
    if (!s_layoutInitialized) {
        s_layoutInitialized = true;
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, viewport->WorkSize);

        ImGuiID topId, bottomId;
        ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Up, 0.55f, &topId, &bottomId);

        ImGui::DockBuilderDockWindow("Browser",   topId);
        ImGui::DockBuilderDockWindow("Inspector", bottomId);
        ImGui::DockBuilderDockWindow("Graph",     bottomId);

        ImGui::DockBuilderFinish(dockId);
    }
    static int s_focusCountdown = 2; // delay focus until windows exist

    ImGui::DockSpace(dockId, {0, 0}, ImGuiDockNodeFlags_None);
    ImGui::End();

    // While the export dialog's load-all thread is running, it mutates the
    // PdbIndex (adding child symbols, populating members, etc.).  Rendering
    // panels that iterate the index concurrently would race against those
    // mutations and crash on vector reallocation.  Skip them until done.
    if (!m_exportDialog.isLoading()) {
        m_browserPanel.render(m_state.get());
        m_inspectorPanel.render(m_state.get());
        //m_graphPanel.render(m_state.get());
        m_graphPanel.simpleRender(m_state.get());
        ImGui::ShowDemoWindow(nullptr);
        // Force Inspector as the active tab after the first couple of frames
        if (s_focusCountdown > 0 && --s_focusCountdown == 0)
            ImGui::SetWindowFocus("Inspector");

        m_searchDialog.render(m_state.get());
    }
    m_exportDialog.render(m_state.get());

    // Export status toast
    if (!m_state->exportStatusMessage.empty()) {
        ImGui::SetNextWindowPos(
            {viewport->WorkPos.x + viewport->WorkSize.x - 10.f,
             viewport->WorkPos.y + viewport->WorkSize.y - 10.f},
            ImGuiCond_Always, {1.f, 1.f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
        ImGui::Begin("##ExportStatus", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs);
        ImGui::TextUnformatted(m_state->exportStatusMessage.c_str());
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // Loading overlay
    if (m_state->loadState.load() == LoadState::Loading) {
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::Begin("##Loading", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
        ImGui::Text("Loading PDB...");
        ImGui::End();
    }

    ImGui::Render();
    ImGuiIO& io = ImGui::GetIO();
    glViewport(0, 0, static_cast<GLsizei>(io.DisplaySize.x), static_cast<GLsizei>(io.DisplaySize.y));
    glClearColor(0.1f, 0.1f, 0.1f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void App::renderMenuBar()
{
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open PDB...", "Ctrl+O"))
            m_state->requestOpen = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) {
            SDL_Event quitEvent{};
            quitEvent.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quitEvent);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Search...", "Ctrl+F", m_state->showSearch)) {
            m_state->showSearch = !m_state->showSearch;
            if (m_state->showSearch) m_state->searchFocusInput = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Prettify Names", nullptr, m_state->prettifyNames)) {
            m_state->prettifyNames = !m_state->prettifyNames;
        }
        ImGui::EndMenu();
    }

    {
        bool hasIndex = m_state->activeIndex != nullptr;
        if (ImGui::MenuItem("Export...", "Ctrl+E", false, hasIndex))
            m_state->showExportDialog = true;
    }

    ImGui::EndMenuBar();
}

void App::showOpenDialog()
{
    static const SDL_DialogFileFilter filters[] = {
        { "PDB files", "pdb" },
        { "All files", "*" },
    };
    SDL_ShowOpenFileDialog(onFileDialogResult, m_state.get(), m_window,
                           filters, SDL_arraysize(filters), nullptr, false);
}

void App::openPdb(const std::string& path)
{
    m_state->activeIndex.reset();
    m_state->selectedSymbol = INVALID_SYMBOL_ID;
    m_state->navBack.clear();
    m_state->navForward.clear();
    m_state->loadState.store(LoadState::Loading);
    m_state->fileExistsCache.clear();

    std::thread(backgroundLoad, m_state.get(), path).detach();
}

void App::onSymbolClicked(SymbolId symbolId)
{
    m_state->loadState.store(LoadState::Loading);
    m_state->selectedSymbol = INVALID_SYMBOL_ID;

    std::thread(backgroundGetSymbolData, m_state.get(), symbolId).detach();
}

