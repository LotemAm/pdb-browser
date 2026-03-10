#pragma once
#include <string>
#include <vector>

struct SymbolNode;
class PdbIndex;

// Target language for code generation.
enum class CodeLang : uint8_t { C, Cpp, Rust };

// Returns a human-readable label for a language (e.g. "C++").
const char* codeLangLabel(CodeLang lang);

// Returns which languages are applicable for the given symbol.
// Empty if the symbol kind doesn't support code generation.
std::vector<CodeLang> applicableLanguages(const SymbolNode& sym);

// Generate a code declaration for `sym` in the given language.
// Returns an empty string if the symbol kind is not supported.
std::string generateCode(const PdbIndex& index, const SymbolNode& sym,
                         CodeLang lang, bool prettify);
