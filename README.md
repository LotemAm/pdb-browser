# PDB Browser

A Windows desktop application for browsing and inspecting Windows debug symbol files (.pdb). Built with C++ and Dear ImGui.

> [!WARNING]  
> This was developed (almost) solely by Claude Code

## Features

- **Symbol Browser** — View compilands, types, and globals with live filtering
- **Inspector** — Detailed view of functions (RVA, size, source location), structs (member layout with offsets), enums, and typedefs
- **Type Graph** — Visual type hierarchy and call graph using imgui-node-editor
- **Search** — Global fuzzy search with filters (symbol kind, source file, size range)
- **Export** — Export dialog (Ctrl+E) with symbol kind selection, nested data toggles, optional bulk detail loading, and JSON/CSV output
- **Name Prettification** — Simplifies C++ and Rust mangled names for readability

## Requirements

- Windows 10/11
- Visual Studio 2022 (for MSVC compiler and DIA SDK)
- CMake 3.25+
- Git

vcpkg is bootstrapped automatically during the first configure if not already present.

## Build

```bash
cmake -B build -S .

# vcpkg location can be configured directly using -DCMAKE_TOOLCHAIN_FILE=
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Release
```

The build will:
1. Clone and bootstrap vcpkg if needed
2. Install dependencies (SDL3, Dear ImGui, imgui-node-editor, nlohmann/json)
3. Locate the DIA SDK from your Visual Studio installation
4. Copy `msdia140.dll` next to the output executable

The resulting binary is at `build/Release/pdb-browser.exe`.

### DIA SDK

The DIA SDK is auto-detected from your Visual Studio installation. If detection fails, set the `DIA_SDK_DIR` environment variable to point to the DIA SDK root (e.g. `C:\Program Files\Microsoft Visual Studio\2022\Community\DIA SDK`).

## Usage

Run `pdb-browser.exe` and open a `.pdb` file via **File > Open** (Ctrl+O). The PDB is parsed on a background thread; once loaded, browse symbols in the top panel, inspect details on the bottom, and explore type relationships in the graph view.

### Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| Ctrl+O | Open PDB file |
| Ctrl+F | Toggle search dialog |
| Ctrl+E | Open export dialog |
| Alt+Left / Mouse 4 | Navigate back |
| Alt+Right / Mouse 5 | Navigate forward |
