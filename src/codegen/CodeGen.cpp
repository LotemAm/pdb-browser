#include "CodeGen.h"
#include "CodeGenC.h"
#include "CodeGenCpp.h"
#include "CodeGenRust.h"
#include "pdb/SymbolNode.h"

const char* codeLangLabel(CodeLang lang)
{
    switch (lang) {
    case CodeLang::C:    return "C";
    case CodeLang::Cpp:  return "C++";
    case CodeLang::Rust: return "Rust";
    default:             return "?";
    }
}

std::vector<CodeLang> applicableLanguages(const SymbolNode& sym)
{
    switch (sym.kind) {
    case SymbolKind::UDT:
    case SymbolKind::Enum:
    case SymbolKind::Function:
        return {CodeLang::C, CodeLang::Cpp, CodeLang::Rust};

    case SymbolKind::Data:
        if (auto* dat = std::get_if<DataSymData>(&sym.kindData))
            if (dat->hasConstValue)
                return {CodeLang::C, CodeLang::Cpp, CodeLang::Rust};
        return {};

    default:
        return {};
    }
}

std::string generateCode(const PdbIndex& index, const SymbolNode& sym,
                         CodeLang lang, bool prettify, const CodeGenOptions& opts)
{
    switch (lang) {
    case CodeLang::C:    return generateC(index, sym, prettify, opts);
    case CodeLang::Cpp:  return generateCpp(index, sym, prettify, opts);
    case CodeLang::Rust: return generateRust(index, sym, prettify, opts);
    default:             return {};
    }
}
