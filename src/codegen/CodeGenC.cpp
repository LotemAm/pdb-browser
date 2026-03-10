#include "CodeGenC.h"
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

static std::string_view typeName(const SymbolNode& sym, bool prettify)
{
    return displayTypeName(sym.typeName, sym.prettyTypeName, prettify);
}

static std::string_view memberType(const MemberInfo& m, bool prettify)
{
    return displayTypeName(m.typeName, m.prettyTypeName, prettify);
}

static const char* udtKeyword(UdtTag tag)
{
    switch (tag) {
    case UdtTag::Union: return "union";
    default:            return "struct";
    }
}

// ── UDT ──────────────────────────────────────────────────────────────────────

static std::string generateUdt(const PdbIndex& /*index*/, const SymbolNode& sym, bool prettify)
{
    const auto& udt = std::get<UdtData>(sym.kindData);
    std::string out;

    auto name = symName(sym, prettify);
    const char* kw = udtKeyword(udt.udtTag);

    out += std::format("typedef {} {} {{\n", kw, name);

    for (const auto& m : udt.members) {
        out += "    ";
        out += memberType(m, prettify);
        out += ' ';
        out += m.name;
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
    auto name = symName(sym, prettify);
    std::string out(name);
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

std::string generateC(const PdbIndex& index, const SymbolNode& sym, bool prettify)
{
    switch (sym.kind) {
    case SymbolKind::UDT:      return generateUdt(index, sym, prettify);
    case SymbolKind::Enum:     return generateEnum(index, sym, prettify);
    case SymbolKind::Function: return generateFunction(index, sym, prettify);
    case SymbolKind::Data:     return generateData(index, sym, prettify);
    default:                   return {};
    }
}
