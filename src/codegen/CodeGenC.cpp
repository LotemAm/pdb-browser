#include "CodeGenC.h"
#include "CodeGen.h"
#include "pdb/PdbIndex.h"
#include "pdb/Prettify.h"
#include "pdb/SymbolNode.h"

#include <format>
#include <string>

// ── helpers ──────────────────────────────────────────────────────────────────

static std::string_view symName(const SymbolNode& sym, bool prettify)
{
    return displayName(sym, prettify);
}

// Remove "ClassName::" qualifier from a function signature.
// Handles the pattern: "rettype ClassName::funcname(..." → "rettype funcname(..."
static std::string stripClassPrefix(std::string s)
{
    auto paren = s.find('(');
    if (paren == std::string::npos) return s;

    auto coloncolon = s.rfind("::", paren);
    if (coloncolon == std::string::npos || coloncolon < 1) return s;

    auto classStart = s.rfind(' ', coloncolon);
    if (classStart == std::string::npos)
        classStart = 0;
    else
        classStart += 1;

    s.erase(classStart, coloncolon + 2 - classStart);
    return s;
}

// Replace "ClassName::" with "ClassName_" in a function signature for C-style naming.
static std::string replaceClassPrefixWithUnderscore(std::string s)
{
    auto paren = s.find('(');
    if (paren == std::string::npos) return s;

    auto coloncolon = s.rfind("::", paren);
    if (coloncolon == std::string::npos || coloncolon < 1) return s;

    s.replace(coloncolon, 2, "_");
    return s;
}

static std::string_view typeName(const SymbolNode& sym, bool prettify)
{
    return displayTypeName(sym.typeName, sym.prettyTypeName, prettify);
}

static std::string_view memberType(const MemberInfo& m, bool prettify)
{
    return displayTypeName(m.typeName, m.prettyTypeName, prettify);
}

// Split "Type[N]" into {"Type", "[N]"}.  If no array suffix, second is empty.
static std::pair<std::string_view, std::string_view> splitArraySuffix(std::string_view type)
{
    if (type.empty() || type.back() != ']') return {type, {}};
    auto open = type.rfind('[');
    if (open == std::string_view::npos) return {type, {}};
    auto base = type.substr(0, open);
    while (!base.empty() && base.back() == ' ') base.remove_suffix(1);
    return {base, type.substr(open)};
}

static const char* udtKeyword(UdtTag tag)
{
    switch (tag) {
    case UdtTag::Union: return "union";
    default:            return "struct";
    }
}

// ── UDT ──────────────────────────────────────────────────────────────────────

static std::string generateUdt(const PdbIndex& index, const SymbolNode& sym, bool prettify,
                               const CodeGenOptions& opts)
{
    const auto& udt = std::get<UdtData>(sym.kindData);
    std::string out;

    auto name = symName(sym, prettify);
    const char* kw = udtKeyword(udt.udtTag);

    out += std::format("typedef {} {} {{\n", kw, name);

    for (const auto& m : udt.members) {
        auto [baseType, arraySuffix] = splitArraySuffix(memberType(m, prettify));
        out += "    ";
        out += baseType;
        out += ' ';
        out += m.name;
        out += arraySuffix;
        if (m.bitSize > 0)
            out += std::format(" : {}", m.bitSize);
        out += ';';

        if (m.bitSize > 0)
            out += std::format(" /* +0x{:X} [{}:{}] */", m.offset, m.bitOffset, m.bitSize);
        else
            out += std::format(" /* +0x{:X} ({} bytes) */", m.offset, m.sizeBytes);
        out += '\n';
    }

    out += std::format("}} {};\n", name);

    // Static members as extern declarations after the struct
    if (opts.includeStatics) {
        for (SymbolId childId : sym.children) {
            const SymbolNode* child = index.getSymbol(childId);
            if (!child || child->kind != SymbolKind::Data) continue;

            out += "extern ";
            auto childType = displayTypeName(child->typeName, child->prettyTypeName, prettify);
            if (!childType.empty()) {
                out += childType;
                out += ' ';
            }
            out += displayName(*child, prettify);
            out += ";\n";
        }
    }

    // Member functions as free functions with ClassName_ prefix
    if (opts.includeFunctions) {
        out += '\n';
        for (SymbolId childId : sym.children) {
            const SymbolNode* child = index.getSymbol(childId);
            if (!child || child->kind != SymbolKind::Function) continue;

            out += replaceClassPrefixWithUnderscore(
                       stripAccessSpecifiers(symName(*child, prettify)));
            out += ";\n";
        }
    }

    return out;
}

// ── Enum ─────────────────────────────────────────────────────────────────────

static std::string generateEnum(const PdbIndex& /*index*/, const SymbolNode& sym, bool prettify)
{
    const auto& en = std::get<EnumData>(sym.kindData);
    std::string out;

    auto name = symName(sym, prettify);
    out += std::format("typedef enum {} {{\n", name);

    for (const auto& ev : en.enumValues) {
        out += std::format("    {} = {},\n", ev.name, ev.value);
    }

    out += std::format("}} {};\n", name);
    return out;
}

// ── Function ─────────────────────────────────────────────────────────────────

static std::string generateFunction(const PdbIndex& /*index*/, const SymbolNode& sym, bool prettify)
{
    auto out = stripClassPrefix(stripAccessSpecifiers(symName(sym, prettify)));
    out += ";\n";
    return out;
}

// ── Data (constant) ──────────────────────────────────────────────────────────

static std::string generateData(const PdbIndex& /*index*/, const SymbolNode& sym, bool prettify)
{
    const auto& dat = std::get<DataSymData>(sym.kindData);
    if (!dat.hasConstValue) return {};

    auto tn = typeName(sym, prettify);
    auto name = symName(sym, prettify);

    std::string out = "const ";
    if (!tn.empty()) {
        out += tn;
        out += ' ';
    }
    out += name;
    out += std::format(" = {};\n", dat.constValue);
    return out;
}

// ── public entry point ───────────────────────────────────────────────────────

std::string generateC(const PdbIndex& index, const SymbolNode& sym, bool prettify,
                      const CodeGenOptions& opts)
{
    switch (sym.kind) {
    case SymbolKind::UDT:      return generateUdt(index, sym, prettify, opts);
    case SymbolKind::Enum:     return generateEnum(index, sym, prettify);
    case SymbolKind::Function: return generateFunction(index, sym, prettify);
    case SymbolKind::Data:     return generateData(index, sym, prettify);
    default:                   return {};
    }
}
