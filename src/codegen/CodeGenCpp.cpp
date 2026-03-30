#include "CodeGenCpp.h"
#include "CodeGen.h"
#include "pdb/PdbIndex.h"
#include "pdb/Prettify.h"
#include "pdb/SymbolNode.h"

#include <format>
#include <string>
#include <vector>

// ── helpers ──────────────────────────────────────────────────────────────────

static std::string_view symName(const SymbolNode& sym, bool prettify)
{
    return displayName(sym, prettify);
}

// Detect access level from DIA undecorated name prefix.
// Returns 0=public, 1=protected, 2=private, 3=unknown.
static int detectAccess(const SymbolNode& sym)
{
    auto name = sym.undecoratedName.empty() ? std::string_view{sym.name}
                                            : std::string_view{sym.undecoratedName};
    if (name.starts_with("public: "))    return 0;
    if (name.starts_with("protected: ")) return 1;
    if (name.starts_with("private: "))   return 2;
    return 3;
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
    case UdtTag::Struct:    return "struct";
    case UdtTag::Class:     return "class";
    case UdtTag::Union:     return "union";
    case UdtTag::Interface: return "struct"; // closest C++ equivalent
    default:                return "struct";
    }
}

// Strip "ClassName::" from a function signature for member function display inside the class body.
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

// ── UDT ──────────────────────────────────────────────────────────────────────

static std::string generateUdt(const PdbIndex& index, const SymbolNode& sym, bool prettify,
                               const CodeGenOptions& opts)
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
        auto [baseType, arraySuffix] = splitArraySuffix(memberType(m, prettify));
        out += "    ";
        out += baseType;
        out += ' ';
        out += m.name;
        out += arraySuffix;
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

    // Static members (from child Data symbols)
    if (opts.includeStatics) {
        for (SymbolId childId : sym.children) {
            const SymbolNode* child = index.getSymbol(childId);
            if (!child || child->kind != SymbolKind::Data) continue;

            out += "    static ";
            auto childType = displayTypeName(child->typeName, child->prettyTypeName, prettify);
            if (!childType.empty()) {
                out += childType;
                out += ' ';
            }
            out += displayName(*child, prettify);
            out += ";\n";
        }
    }

    // Member functions grouped by access level
    if (opts.includeFunctions) {
        // Bucket: 0=public, 1=protected, 2=private, 3=unknown
        std::vector<SymbolId> buckets[4];
        for (SymbolId childId : sym.children) {
            const SymbolNode* child = index.getSymbol(childId);
            if (!child || child->kind != SymbolKind::Function) continue;
            buckets[detectAccess(*child)].push_back(childId);
        }

        static const char* labels[] = {"public:\n", "protected:\n", "private:\n", nullptr};
        for (int a = 0; a < 4; ++a) {
            if (buckets[a].empty()) continue;
            if (labels[a]) {
                out += '\n';
                out += labels[a];
            }
            for (SymbolId fId : buckets[a]) {
                const SymbolNode* fn = index.getSymbol(fId);
                if (!fn) continue;
                out += "    ";
                out += stripClassPrefix(stripAccessSpecifiers(symName(*fn, prettify)));
                out += ";\n";
            }
        }
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
    auto out = stripAccessSpecifiers(symName(sym, prettify));
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

std::string generateCpp(const PdbIndex& index, const SymbolNode& sym, bool prettify,
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
