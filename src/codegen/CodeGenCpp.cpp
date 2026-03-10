#include "CodeGenCpp.h"
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
    case UdtTag::Struct:    return "struct";
    case UdtTag::Class:     return "class";
    case UdtTag::Union:     return "union";
    case UdtTag::Interface: return "struct"; // closest C++ equivalent
    default:                return "struct";
    }
}

// ── UDT ──────────────────────────────────────────────────────────────────────

static std::string generateUdt(const PdbIndex& index, const SymbolNode& sym, bool prettify)
{
    const auto& udt = std::get<UdtData>(sym.kindData);
    std::string out;

    auto name = symName(sym, prettify);
    out += udtKeyword(udt.udtTag);
    out += ' ';
    out += name;

    // Base classes
    if (!udt.baseClasses.empty()) {
        out += " : ";
        for (size_t i = 0; i < udt.baseClasses.size(); ++i) {
            if (i > 0) out += ", ";
            out += "public ";
            const SymbolNode* bc = index.getSymbol(udt.baseClasses[i]);
            if (bc)
                out += displayName(*bc, prettify);
            else
                out += "?";
        }
    }

    out += " {\n";

    for (const auto& m : udt.members) {
        out += "    ";
        out += memberType(m, prettify);
        out += ' ';
        out += m.name;
        if (m.bitSize > 0)
            out += std::format(" : {}", m.bitSize);
        out += ';';

        // Offset comment
        if (m.bitSize > 0)
            out += std::format(" // +0x{:X} [{}:{}]", m.offset, m.bitOffset, m.bitSize);
        else
            out += std::format(" // +0x{:X} ({} bytes)", m.offset, m.sizeBytes);
        out += '\n';
    }

    out += "};\n";
    return out;
}

// ── Enum ─────────────────────────────────────────────────────────────────────

static std::string generateEnum(const PdbIndex& /*index*/, const SymbolNode& sym, bool prettify)
{
    const auto& en = std::get<EnumData>(sym.kindData);
    std::string out;

    auto name = symName(sym, prettify);
    out += "enum class ";
    out += name;

    auto underlying = typeName(sym, prettify);
    if (!underlying.empty()) {
        out += " : ";
        out += underlying;
    }

    out += " {\n";

    for (const auto& ev : en.enumValues) {
        out += std::format("    {} = {},\n", ev.name, ev.value);
    }

    out += "};\n";
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

    std::string out = "constexpr ";
    if (!tn.empty()) {
        out += tn;
        out += ' ';
    }
    out += name;
    out += std::format(" = {};\n", dat.constValue);
    return out;
}

// ── public entry point ───────────────────────────────────────────────────────

std::string generateCpp(const PdbIndex& index, const SymbolNode& sym, bool prettify)
{
    switch (sym.kind) {
    case SymbolKind::UDT:      return generateUdt(index, sym, prettify);
    case SymbolKind::Enum:     return generateEnum(index, sym, prettify);
    case SymbolKind::Function: return generateFunction(index, sym, prettify);
    case SymbolKind::Data:     return generateData(index, sym, prettify);
    default:                   return {};
    }
}
