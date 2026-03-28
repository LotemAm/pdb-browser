#pragma once
#include "CodeGen.h"
#include <string>

struct SymbolNode;
class PdbIndex;

// Generate a Rust declaration for the given symbol.
std::string generateRust(const PdbIndex& index, const SymbolNode& sym, bool prettify,
                         const CodeGenOptions& opts = {});
