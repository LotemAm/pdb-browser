#include "ExportDialog.h"
#include "app/AppState.h"
#include "pdb/PdbIndex.h"
#include "pdb/PdbSession.h"

#include <imgui.h>
#include <SDL3/SDL.h>

#include <format>
#include <thread>

// ── SDL save-dialog callback ─────────────────────────────────────────────────

// We stash a pointer to the ExportDialog's m_pendingPath string.
static void SDLCALL onExportSaveResult(void* userdata, const char* const* filelist, int /*filter*/)
{
    auto* path = static_cast<std::string*>(userdata);
    if (!filelist || !*filelist) {
        path->clear();   // cancelled
        return;
    }
    *path = *filelist;
}

// ── background load-all thread ──────────────────────────────────────────────

static void backgroundLoadAll(AppState* state,
                               std::vector<SymbolId> ids,
                               std::atomic<int>* progress,
                               std::atomic<bool>* cancel,
                               std::atomic<bool>* loading)
{
    int done = 0;
    for (SymbolId id : ids) {
        if (cancel->load()) break;

        // Skip symbols that already have their detail data loaded
        const SymbolNode* sym = state->activeIndex->getSymbol(id);
        if (sym && sym->childrenLoaded) {
            ++done;
            progress->store(done);
            continue;
        }

        // Load detail data via DIA (COM is already initialized on this thread's session)
        (void)state->activePdbSession->loadSymbolData(state->activeIndex, id);
        ++done;
        progress->store(done);
    }

    loading->store(false);
}

// ── ExportDialog ─────────────────────────────────────────────────────────────

void ExportDialog::collectIds(const AppState* state)
{
    m_exportIds.clear();
    if (!state->activeIndex) return;

    if (m_selectedOnly && state->selectedSymbol != INVALID_SYMBOL_ID) {
        m_exportIds.push_back(state->selectedSymbol);
        return;
    }

    const PdbIndex& idx = *state->activeIndex;

    auto append = [&](const std::vector<SymbolId>& src) {
        m_exportIds.insert(m_exportIds.end(), src.begin(), src.end());
    };

    if (m_exportUdts)       append(idx.udts());
    if (m_exportEnums)      append(idx.enums());
    if (m_exportFunctions)  append(idx.functions());
    if (m_exportCompilands) append(idx.compilands());
    if (m_exportTypedefs)   append(idx.typedefs());
    if (m_exportGlobals)    append(idx.globals());
}

void ExportDialog::startLoadAll(AppState* state)
{
    collectIds(state);
    if (m_exportIds.empty()) return;

    m_loadTotal = static_cast<int>(m_exportIds.size());
    m_loadProgress.store(0);
    m_cancelLoading.store(false);
    m_loading.store(true);

    std::thread(backgroundLoadAll, state, m_exportIds,
                &m_loadProgress, &m_cancelLoading, &m_loading).detach();
}

void ExportDialog::showSaveDialog(AppState* /*state*/)
{
    const char* ext  = (m_format == 0) ? "json" : "csv";
    const char* desc = (m_format == 0) ? "JSON files" : "CSV files";

    static SDL_DialogFileFilter filters[2];
    filters[0] = { desc, ext };
    filters[1] = { "All files", "*" };

    m_pendingPath.clear();
    // Pass pointer to m_pendingPath — the callback writes into it
    SDL_ShowSaveFileDialog(onExportSaveResult, &m_pendingPath, nullptr,
                           filters, 2, nullptr);
}

void ExportDialog::executeExport(AppState* state)
{
    if (!state->activeIndex || m_pendingPath.empty()) return;

    collectIds(state);
    if (m_exportIds.empty()) {
        state->exportStatusMessage = "Nothing to export.";
        state->exportStatusTimer = 3.f;
        return;
    }

    ExportOptions opts;
    opts.includeMembers      = m_includeMembers;
    opts.includeBaseClasses  = m_includeBaseClasses;
    opts.includeFriends      = m_includeFriends;
    opts.includeEnumValues   = m_includeEnumValues;
    opts.includeSourceFiles  = m_includeSourceFiles;
    opts.includeTemplateArgs = m_includeTemplateArgs;

    std::expected<void, std::string> result;
    if (m_format == 0)
        result = Exporter::toJson(*state->activeIndex, m_exportIds, m_pendingPath, opts);
    else
        result = Exporter::toCsv(*state->activeIndex, m_exportIds, m_pendingPath, opts);

    if (result) {
        state->exportStatusMessage = std::format(
            "Exported {} symbols to {}", m_exportIds.size(), m_pendingPath);
    } else {
        state->exportStatusMessage = std::format("Export failed: {}", result.error());
    }
    state->exportStatusTimer = 5.f;
    m_pendingPath.clear();
}

void ExportDialog::render(AppState* state)
{
    if (!state->showExportDialog) return;

    // Center on first open
    if (m_needsPosition) {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowSize({460, 0}, ImGuiCond_Always);
        m_needsPosition = false;
    }

    if (!ImGui::Begin("Export", &state->showExportDialog,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    // Escape closes
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !m_loading.load())
        state->showExportDialog = false;

    if (!state->activeIndex) {
        ImGui::TextDisabled("No PDB loaded.");
        ImGui::End();
        return;
    }

    const bool isLoading = m_loading.load();

    // Disable controls while background loading is in progress
    if (isLoading) ImGui::BeginDisabled();

    // ── Format ───────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Format");
    ImGui::RadioButton("JSON", &m_format, 0);
    ImGui::SameLine();
    ImGui::RadioButton("CSV", &m_format, 1);

    // ── Scope ────────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Scope");
    bool hasSelection = state->selectedSymbol != INVALID_SYMBOL_ID;
    if (!hasSelection) ImGui::BeginDisabled();
    if (ImGui::RadioButton("Selected symbol only", m_selectedOnly))
        m_selectedOnly = true;
    if (!hasSelection) ImGui::EndDisabled();
    if (ImGui::RadioButton("All matching symbols", !m_selectedOnly))
        m_selectedOnly = false;

    // ── Symbol Kinds ─────────────────────────────────────────────────────────
    if (!m_selectedOnly) {
        ImGui::SeparatorText("Symbol Kinds");

        auto kindRow = [](const char* label, bool* enabled, auto renderNested) {
            ImGui::Checkbox(label, enabled);
            if (*enabled) {
                ImGui::Indent(20.f);
                renderNested();
                ImGui::Unindent(20.f);
            }
        };

        kindRow("Types (UDTs)", &m_exportUdts, [&] {
            ImGui::Checkbox("Members##udt",       &m_includeMembers);
            ImGui::SameLine();
            ImGui::Checkbox("Base classes##udt",   &m_includeBaseClasses);
            ImGui::SameLine();
            ImGui::Checkbox("Friends##udt",        &m_includeFriends);
            ImGui::Checkbox("Template args##udt",  &m_includeTemplateArgs);
        });

        kindRow("Enums", &m_exportEnums, [&] {
            ImGui::Checkbox("Enum values##enum", &m_includeEnumValues);
        });

        kindRow("Functions", &m_exportFunctions, [&] {
            ImGui::Checkbox("Template args##fn", &m_includeTemplateArgs);
        });

        kindRow("Compilands", &m_exportCompilands, [&] {
            ImGui::Checkbox("Source files##comp", &m_includeSourceFiles);
        });

        ImGui::Checkbox("Typedefs", &m_exportTypedefs);
        ImGui::Checkbox("Globals",  &m_exportGlobals);
    }

    // ── Load All ─────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Detail Data");
    ImGui::Checkbox("Load all detail data before export", &m_loadAll);
    if (m_loadAll) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.8f, 0.2f, 1.f));
        ImGui::TextWrapped("Warning: This will load detailed data (members, enum values, "
                           "source info) for every selected symbol via DIA. "
                           "This can take significant time and memory for large PDBs.");
        ImGui::PopStyleColor();
    }

    if (isLoading) ImGui::EndDisabled();

    // ── Progress bar (while loading) ─────────────────────────────────────────
    if (isLoading) {
        int done  = m_loadProgress.load();
        float frac = (m_loadTotal > 0)
            ? static_cast<float>(done) / static_cast<float>(m_loadTotal)
            : 0.f;
        std::string overlay = std::format("Loading {}/{}", done, m_loadTotal);
        ImGui::ProgressBar(frac, {-1, 0}, overlay.c_str());

        if (ImGui::Button("Cancel")) {
            m_cancelLoading.store(true);
        }

        // Check if loading just finished
        // (m_loading was set to false by the background thread)
    } else {
        // Check if load-all just completed — proceed to save dialog
        if (m_loadTotal > 0 && m_loadProgress.load() >= m_loadTotal) {
            // Reset tracking so this doesn't fire again
            m_loadTotal = 0;
            m_loadProgress.store(0);

            if (!m_cancelLoading.load()) {
                showSaveDialog(state);
            }
            m_cancelLoading.store(false);
        }
    }

    // ── Buttons ──────────────────────────────────────────────────────────────
    if (!isLoading) {
        ImGui::Separator();
        bool anyKind = m_selectedOnly ||
            m_exportUdts || m_exportEnums || m_exportFunctions ||
            m_exportCompilands || m_exportTypedefs || m_exportGlobals;

        if (!anyKind) ImGui::BeginDisabled();
        if (ImGui::Button("Export...", {120, 0})) {
            if (m_loadAll) {
                startLoadAll(state);
            } else {
                showSaveDialog(state);
            }
        }
        if (!anyKind) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0}))
            state->showExportDialog = false;
    }

    // ── Handle save dialog result ────────────────────────────────────────────
    if (!m_pendingPath.empty()) {
        executeExport(state);
        state->showExportDialog = false;
    }

    ImGui::End();

    // Reset positioning flag when dialog closes
    if (!state->showExportDialog)
        m_needsPosition = true;
}
