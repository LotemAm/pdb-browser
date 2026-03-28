#pragma once
#include "CodeGen.h"
#include <string>

struct SymbolNode;
class PdbIndex;

// Generate a C++ declaration for the given symbol.
std::string generateCpp(const PdbIndex& index, const SymbolNode& sym, bool prettify,
                        const CodeGenOptions& opts = {});
