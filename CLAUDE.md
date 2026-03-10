# PDB Browser — Claude Project Context

## Project Overview

A Windows desktop application for browsing, inspecting, visualizing, and exporting data from **Windows debug symbol files (.pdb)**. Built with C++ and Dear ImGui, targeting power users (reverse engineers, compiler/linker developers, engine programmers).

## Tech Stack

| Layer | Technology |
|---|---|
| Language | C++26 |
| GUI | Dear ImGui (docking branch) |
| Windowing / Input | SDL3 + OpenGL |
| Graph visualization | imgui-node-editor |
| PDB parsing | Microsoft DIA SDK (COM) |
| Build system | CMake + vcpkg |
| Serialization | nlohmann/json |

## Project Structure

```
pdb-browser/
├── CMakeLists.txt
├── vcpkg.json
├── CLAUDE.md
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── App.h / App.cpp         # Main loop, docking layout, menu bar
│   │   └── AppState.h              # Shared state: selection, nav history, UI flags
│   ├── pdb/
│   │   ├── PdbSession.h/.cpp       # DIA SDK wrapper (open, close, enumerate)
│   │   ├── SymbolNode.h            # Unified symbol struct + kind-specific data variants
│   │   ├── PdbIndex.h/.cpp         # In-memory indexes (by name, RVA, type)
│   │   ├── Prettify.h/.cpp         # Public API: displayName(), displayTypeName(), prettifyName()
│   │   ├── PrettifyCpp.h/.cpp      # C++ prettification rules (lambda, std::, calling conv, etc.)
│   │   └── PrettifyRust.h/.cpp     # Rust prettification rules (alloc::, core::, closure$, etc.)
│   ├── ui/
│   │   ├── BrowserPanel.h/.cpp     # Tabbed tables: Types, Enums, Functions, Compilands, Typedefs, Globals
│   │   ├── InspectorPanel.h/.cpp   # Detail view with nav buttons, source links, clickable types
│   │   ├── GraphPanel.h/.cpp       # Node graph: type hierarchy via imgui-node-editor
│   │   ├── SearchDialog.h/.cpp     # Global search + kind/size filters
│   │   └── ExportDialog.h/.cpp     # Export settings dialog with kind/data toggles + load-all
│   └── export/
│       └── Exporter.h/.cpp         # CSV and JSON export (ExportOptions for nested data control)
└── cmake/
    └── FindDIASDK.cmake            # Locates DIA SDK via vswhere
```

## Architecture

- **PDB Backend** is fully decoupled from UI. All parsing lives in `pdb/`, all rendering in `ui/`.
- **AppState** is the shared state bridge between backend and UI — no direct cross-panel coupling.
- **PDB parsing runs on a background thread**. The main thread polls a loading state and shows a progress bar. Once complete, the index is swapped in atomically.
- **Lazy loading** for symbol tree children — never enumerate all children up front on large PDBs.
- **Kind-specific data** uses `std::variant` on `SymbolNode::kindData` (`FunctionData`, `UdtData`, `EnumData`, `CompilandData`, `DataSymData`) — access with `std::get<T>(sym.kindData)`.

## DIA SDK Usage

DIA SDK ships with Visual Studio:
- Headers: `%VSINSTALLDIR%\DIA SDK\include`
- Libs: `%VSINSTALLDIR%\DIA SDK\lib\amd64\`

Core pattern for opening a PDB:
```cpp
CoInitialize(nullptr);
CComPtr<IDiaDataSource> source;
CoCreateInstance(CLSID_DiaSource, nullptr, CLSCTX_INPROC_SERVER,
                 IID_IDiaDataSource, (void**)&source);
source->loadDataFromPdb(L"file.pdb");
CComPtr<IDiaSession> session;
source->openSession(&session);
CComPtr<IDiaSymbol> global;
session->get_globalScope(&global);
```

Key DIA interfaces used:
- `IDiaDataSource` — load PDB file
- `IDiaSession` — query entry point
- `IDiaSymbol` — any symbol (function, UDT, enum, typedef, compiland, etc.)
- `IDiaEnumSymbols` — iterate children
- `IDiaEnumLineNumbers` — source line info
- `IDiaEnumSourceFiles` — source file list

## Key Conventions

### COM Safety
- Always use `CComPtr<>` for all DIA COM objects — never raw pointers.
- Call `CoInitialize` / `CoUninitialize` on **every thread** that touches DIA (including the background parsing thread).
- Never pass raw `IDia*` pointers across thread boundaries — serialize through `AppState` after parsing is complete.

### Symbol Selection & Navigation
- Always use `AppState::selectSymbol(id)` to change the selected symbol — never assign `state->selectedSymbol` directly.
- `selectSymbol()` pushes to `navBack`, clears `navForward`, then calls `App::onSymbolClicked()` which triggers lazy loading of the symbol's detail data on a background thread.
- Back/forward navigation: mouse buttons 4/5 or Alt+Left/Right. Managed by `AppState::navigateBack()` / `navigateForward()`.

### Symbol Node
- `SymbolNode` is the canonical in-memory representation of any symbol. It must not hold live COM pointers after indexing — extract all needed data during parsing and store it as plain C++ types.
- Kind-specific data is stored in `SymbolNode::kindData` as a `std::variant<std::monostate, FunctionData, CompilandData, UdtData, EnumData, DataSymData>`. Access with `std::get<T>(sym.kindData)`.
- Template arguments are parsed at load time into `SymbolNode::templateArgs` and resolved to `SymbolId` where possible.

### Name Prettification
- Prettified names (`prettyName`, `prettyTypeName`) are pre-computed at symbol load time in `PdbSession` via `prettifyName()` and stored on `SymbolNode` / `MemberInfo` / `TemplateArg`.
- UI code reads pre-computed fields via `displayName(sym, prettify)` and `displayTypeName(raw, pretty, prettify)` — no per-frame computation.
- The prettify toggle (`AppState::prettifyNames`) simply switches which pre-computed field the UI reads.
- C++ rules (`PrettifyCpp.cpp`): lambda cleanup, calling convention removal, `class`/`struct`/`enum` qualifier removal, `std::basic_string` → `std::string` collapsing, default template arg collapsing, whitespace normalization.
- Rust rules (`PrettifyRust.cpp`): shorten `alloc::`/`core::`/`std::` paths, clean `closure$N` → `<closure>`, `impl$N` → `impl`.
- Both rulesets are applied unconditionally — they don't conflict.

### Memory
- Large PDBs (e.g. Chromium, Windows kernel) can have millions of symbols. Use flat `std::vector` + `std::unordered_map` indexes. Avoid tree-of-pointers structures.
- Symbol children in the browser panel are loaded on first expand, not on PDB open.

### Threading
- One background thread for initial PDB load + indexing.
- Main thread is UI-only — never block it with DIA calls.
- Use `std::atomic<LoadState>` + a mutex-protected result struct to hand off data.
- **Export load-all thread safety**: The export dialog's "load all detail data" feature spawns a background thread that calls `loadSymbolData()` for every selected symbol. This mutates the `PdbIndex` (adding child symbols via `addSymbol()` → `m_symbols.push_back()`), which can reallocate the vector and invalidate all `SymbolNode*` pointers. While this thread is running, **all panels that iterate the index must be skipped** — guarded by `ExportDialog::isLoading()` in `App::render()`.

### Naming
- Files: `PascalCase.h / PascalCase.cpp`
- Classes/structs: `PascalCase`
- Functions/methods: `camelCase`
- Member variables: `m_camelCase`
- Constants / enums: `ALL_CAPS` or `PascalCase` enum classes

## UI Layout

```
┌──────────────────────────────────────────────────────────────┐
│  Menu Bar: File | View | Export...                             │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  Browser (tabbed tables)                                     │
│  Types | Enums | Functions | Compilands | Typedefs | Globals │
│  [filter input]                                              │
│                                                              │
├──────────────────────────────────────────────────────────────┤
│  Inspector (detail view)  |  Graph (type hierarchy)          │
│  ← → nav  | raw title    |  imgui-node-editor               │
│            | pretty name  |                                  │
│            | properties   |                                  │
│                                                              │
└──────────────────────────────────────────────────────────────┘
  Search dialog (Ctrl+F) — floating overlay
  Export dialog (Ctrl+E) — floating overlay
```

All panels are ImGui docking windows — user can freely rearrange. Default layout: Browser top half, Inspector and Graph tabbed in bottom half.

## Features

### Browser Panel
- Tabbed tables: Types, Enums, Functions, Compilands, Typedefs, Globals
- Clicking a row selects the symbol and populates Inspector and Graph panels
- Live filter input searches across raw name, undecorated name, and prettified name (via `symbolMatchesFilter`)
- Displays prettified names when toggle is on

### Inspector Panel
- Back/forward navigation buttons (with mouse 4/5 and Alt+arrow keyboard shortcuts)
- Header shows raw name as title, prettified name as secondary (dimmed) when it differs
- **Functions**: return type (clickable), RVA, size, calling convention, inline/virtual/static flags, parent class (clickable), source file (clickable link), template arguments
- **UDTs**: size, base classes (clickable), friends (clickable), data members table (offset/name/type/size with clickable types), member functions table, vtable (collapsible), template arguments
- **Enums**: size, underlying type, enumerator name/value table
- **Typedefs**: alias name, resolved target type (clickable), size
- **Data symbols**: data kind, RVA, type (clickable), size, constant value, source location
- Source file paths render as clickable links (opens in default editor) or show "not found" indicator

### Graph Panel
- **Type graph**: UDT inheritance chain + member types as nodes/edges (imgui-node-editor)
- Pan, zoom, click-to-inspect any node
- Rebuilds when selected symbol or prettify toggle changes
- Cycle detection — PDB type graphs can be cyclic

### Search
- Global search across all symbol names (raw + prettified)
- Filters: symbol kind (function / UDT / enum / global / typedef)
- Results shown in a table; clicking a result selects the symbol

### Export
- **Export dialog** (Ctrl+E or menu bar) — self-contained floating overlay with all settings as member variables (minimal AppState footprint: only `showExportDialog` flag)
- **Scope**: Export selected symbol only or all matching symbols
- **Symbol kind toggles**: UDTs, Enums, Functions, Compilands, Typedefs, Globals — each with per-kind nested data sub-checkboxes:
  - UDTs → Members, Base classes, Friends, Template args
  - Enums → Enum values
  - Functions → Template args
  - Compilands → Source files
- **Formats**: JSON (structured, with nested data) or CSV (flat)
- **Load all detail data**: Optional pre-export step that loads detailed data (members, enum values, source files) for every selected symbol via DIA. Shows a progress bar in the dialog; blocks panel rendering to prevent data races from `PdbIndex` vector reallocation.
- **ExportOptions struct** (`Exporter.h`) controls which nested data sections appear in output — maps directly to the dialog's nested data checkboxes.
- Copy individual field values to clipboard from Inspector

## Gotchas & Known Issues

- **DIA SDK is MSVC + Windows only** — do not attempt cross-platform builds.
- **Cyclic type graphs** — always track visited nodes when traversing type relationships.
- **Very large PDBs** — lazy-load everything; never enumerate all symbols eagerly.
- **DIA COM apartment** — DIA uses STA; background thread must call `CoInitialize(nullptr)` (not `CoInitializeEx` with MTA) unless tested otherwise.
- **Missing PDB data** — not all PDBs have call graph info. Degrade gracefully: hide the call graph tab rather than crashing.
- **RVA vs VA** — keep RVA (relative) internally; only convert to VA when displaying with a known image base.
- **DIA HRESULT checks** — use `== S_OK` (not `SUCCEEDED()`) for DIA calls; `S_FALSE` (1) passes `SUCCEEDED()` but means "no result". `FAILED()` is still correct for early-return error checks.
- **Background `PdbIndex` mutation** — any background thread that calls `loadSymbolData()` or `addSymbol()` can trigger `m_symbols` vector reallocation, invalidating all `SymbolNode*` pointers. The main thread must not iterate the index during such operations. See `ExportDialog::isLoading()` guard in `App::render()`.

## Build Instructions

```bash
# Prerequisites: Visual Studio 2022, CMake 3.25+, vcpkg
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

DIA SDK path must be discoverable — set `DIA_SDK_DIR` env var or hardcode in `CMakeLists.txt`.

New `.cpp` files in `src/` are auto-discovered by `GLOB_RECURSE` in CMakeLists.txt — no need to manually add them.