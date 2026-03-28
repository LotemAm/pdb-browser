#include "CodeGenRust.h"
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

// Strip leading "public: ", "private: ", "protected: " from DIA undecorated names.
static std::string stripAccessSpecifiers(std::string_view s)
{
    for (auto prefix : {"public: ", "private: ", "protected: "}) {
        if (s.starts_with(prefix)) {
            s.remove_prefix(std::string_view{prefix}.size());
            break;
        }
    }
    return std::string{s};
}

// Convert a C++ type string to Rust-style pointer/reference syntax.
//   "int *"          → "*mut int"
//   "const int *"    → "*const int"
//   "int &"          → "&int"
//   "const int &"    → "&int"
//   "int * *"        → "*mut *mut int"
//   "Foo"            → "Foo"  (no change)
static std::string toRustType(std::string_view sv)
{
    // Trim
    while (!sv.empty() && sv.back() == ' ')  sv.remove_suffix(1);
    while (!sv.empty() && sv.front() == ' ') sv.remove_prefix(1);
    if (sv.empty()) return {};

    // Peel trailing pointer: "... *"
    if (sv.back() == '*') {
        auto base = sv.substr(0, sv.size() - 1);
        while (!base.empty() && base.back() == ' ') base.remove_suffix(1);

        // Check if the base type is const-qualified
        // "const T" or "T const" patterns
        auto inner = toRustType(base);
        if (base.starts_with("const ")) {
            // "const T *" → *const T  (strip the const we already accounted for)
            auto stripped = base.substr(6);
            while (!stripped.empty() && stripped.front() == ' ') stripped.remove_prefix(1);
            return "*const " + toRustType(stripped);
        }
        // Check for east-const: "T const"
        if (base.ends_with(" const")) {
            auto stripped = base.substr(0, base.size() - 6);
            while (!stripped.empty() && stripped.back() == ' ') stripped.remove_suffix(1);
            return "*const " + toRustType(stripped);
        }
        return "*mut " + inner;
    }

    // Peel trailing reference: "... &"
    if (sv.back() == '&') {
        auto base = sv.substr(0, sv.size() - 1);
        while (!base.empty() && base.back() == ' ') base.remove_suffix(1);

        // Strip const for references — Rust references are always immutable by default
        if (base.starts_with("const ")) {
            auto stripped = base.substr(6);
            while (!stripped.empty() && stripped.front() == ' ') stripped.remove_prefix(1);
            return "&" + toRustType(stripped);
        }
        if (base.ends_with(" const")) {
            auto stripped = base.substr(0, base.size() - 6);
            while (!stripped.empty() && stripped.back() == ' ') stripped.remove_suffix(1);
            return "&" + toRustType(stripped);
        }
        return "&mut " + toRustType(base);
    }

    // Peel trailing array: "Type[N]" → "[Type; N]"
    if (sv.back() == ']') {
        auto open = sv.rfind('[');
        if (open != std::string_view::npos) {
            auto base = sv.substr(0, open);
            while (!base.empty() && base.back() == ' ') base.remove_suffix(1);
            auto dim = sv.substr(open + 1, sv.size() - open - 2); // N
            return "[" + toRustType(base) + "; " + std::string{dim} + "]";
        }
    }

    return std::string{sv};
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

// Split a comma-separated parameter list respecting nested <> and () depth.
// Returns individual parameter strings (trimmed).
static std::vector<std::string_view> splitParams(std::string_view params)
{
    std::vector<std::string_view> result;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < params.size(); ++i) {
        char c = params[i];
        if (c == '<' || c == '(') ++depth;
        else if (c == '>' || c == ')') --depth;
        else if (c == ',' && depth == 0) {
            auto p = params.substr(start, i - start);
            while (!p.empty() && p.front() == ' ') p.remove_prefix(1);
            while (!p.empty() && p.back() == ' ')  p.remove_suffix(1);
            result.push_back(p);
            start = i + 1;
        }
    }
    auto last = params.substr(start);
    while (!last.empty() && last.front() == ' ') last.remove_prefix(1);
    while (!last.empty() && last.back() == ' ')  last.remove_suffix(1);
    if (!last.empty()) result.push_back(last);
    return result;
}

// Convert a comma-separated C++ parameter list to Rust types.
static std::string convertParamsToRust(std::string_view params)
{
    auto parts = splitParams(params);
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += ", ";
        out += toRustType(parts[i]);
    }
    return out;
}

// Transform a C++ function signature into Rust-style syntax.
// Input (after access specifier stripped):
//   "[virtual] [static] ReturnType [ClassName::]FuncName(params) [const]"
// Output:
//   "fn func_name(&self, params) -> ReturnType"   (or no -> if void)
static std::string toRustSignature(std::string_view sig, bool isMember)
{
    std::string s{sig};

    // Strip leading keywords
    bool isStatic = false;
    for (;;) {
        if (s.starts_with("virtual ")) {
            s.erase(0, 8);
        } else if (s.starts_with("static ")) {
            isStatic = true;
            s.erase(0, 7);
        } else {
            break;
        }
    }

    // Strip calling conventions (e.g. __cdecl, __stdcall, __thiscall, __fastcall)
    for (auto cc : {"__cdecl ", "__stdcall ", "__thiscall ", "__fastcall ", "__vectorcall "}) {
        auto pos = s.find(cc);
        if (pos != std::string::npos) {
            s.erase(pos, std::string_view{cc}.size());
            break;
        }
    }

    // Strip trailing "const" after the closing paren
    auto closeParen = s.rfind(')');
    if (closeParen != std::string::npos) {
        auto tail = std::string_view{s}.substr(closeParen + 1);
        while (!tail.empty() && tail.front() == ' ') tail.remove_prefix(1);
        if (tail == "const")
            s.erase(closeParen + 1);
    }

    // Find the '(' to split return type + name from params
    auto openParen = s.find('(');
    if (openParen == std::string::npos)
        return "fn " + s;

    auto beforeParen = std::string_view{s}.substr(0, openParen);

    // Extract raw params between parens
    auto rawParams = std::string_view{s}.substr(openParen + 1);
    auto closeP = rawParams.rfind(')');
    if (closeP != std::string_view::npos)
        rawParams = rawParams.substr(0, closeP);
    while (!rawParams.empty() && rawParams.front() == ' ') rawParams.remove_prefix(1);
    while (!rawParams.empty() && rawParams.back() == ' ')  rawParams.remove_suffix(1);

    // The function name is the last space-separated token before '('
    // Everything before it is the return type.
    auto lastSpace = beforeParen.rfind(' ');
    std::string_view returnType;
    std::string_view funcName;
    if (lastSpace == std::string_view::npos) {
        funcName = beforeParen;
    } else {
        returnType = beforeParen.substr(0, lastSpace);
        funcName = beforeParen.substr(lastSpace + 1);
    }

    // Strip ClassName:: from funcName
    auto cc = funcName.rfind("::");
    if (cc != std::string_view::npos)
        funcName = funcName.substr(cc + 2);

    // Build the Rust signature
    std::string out = "fn ";
    out += funcName;
    out += '(';

    // Params — inject &self for non-static member functions
    bool isVoid = rawParams.empty() || rawParams == "void";
    if (isMember && !isStatic) {
        out += "&self";
        if (!isVoid)
            out += ", " + convertParamsToRust(rawParams);
    } else {
        if (!isVoid)
            out += convertParamsToRust(rawParams);
    }
    out += ')';

    // Return type
    while (!returnType.empty() && returnType.front() == ' ') returnType.remove_prefix(1);
    while (!returnType.empty() && returnType.back() == ' ')  returnType.remove_suffix(1);

    if (!returnType.empty() && returnType != "void")
        out += " -> " + toRustType(returnType);

    return out;
}

static std::string rustTypeName(const SymbolNode& sym, bool prettify)
{
    return toRustType(displayTypeName(sym.typeName, sym.prettyTypeName, prettify));
}

static std::string rustMemberType(const MemberInfo& m, bool prettify)
{
    return toRustType(displayTypeName(m.typeName, m.prettyTypeName, prettify));
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

    out += "#[repr(C)]\n";
    out += "pub ";
    out += udtKeyword(udt.udtTag);
    out += ' ';
    out += name;
    out += " {\n";

    for (const auto& m : udt.members) {
        out += "    pub ";
        out += m.name;
        out += ": ";
        out += rustMemberType(m, prettify);
        out += ',';

        if (m.bitSize > 0)
            out += std::format(" // +0x{:X} [{}:{}]", m.offset, m.bitOffset, m.bitSize);
        else
            out += std::format(" // +0x{:X} ({} bytes)", m.offset, m.sizeBytes);
        out += '\n';
    }

    out += "}\n";

    // impl block for statics and/or functions
    bool needsImpl = (opts.includeStatics || opts.includeFunctions);
    bool hasContent = false;

    if (needsImpl) {
        // Check if there's any content to emit
        for (SymbolId childId : sym.children) {
            const SymbolNode* child = index.getSymbol(childId);
            if (!child) continue;
            if (opts.includeStatics && child->kind == SymbolKind::Data) { hasContent = true; break; }
            if (opts.includeFunctions && child->kind == SymbolKind::Function) { hasContent = true; break; }
        }
    }

    if (hasContent) {
        out += std::format("\nimpl {} {{\n", name);

        // Static members
        if (opts.includeStatics) {
            for (SymbolId childId : sym.children) {
                const SymbolNode* child = index.getSymbol(childId);
                if (!child || child->kind != SymbolKind::Data) continue;

                out += "    pub static ";
                out += displayName(*child, prettify);
                auto childType = rustTypeName(*child, prettify);
                if (!childType.empty()) {
                    out += ": ";
                    out += childType;
                }
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

            static const char* labels[] = {
                "    // public\n", "    // protected\n", "    // private\n", nullptr
            };
            for (int a = 0; a < 4; ++a) {
                if (buckets[a].empty()) continue;
                if (labels[a]) {
                    out += '\n';
                    out += labels[a];
                }
                for (SymbolId fId : buckets[a]) {
                    const SymbolNode* fn = index.getSymbol(fId);
                    if (!fn) continue;
                    auto cleaned = stripAccessSpecifiers(symName(*fn, prettify));
                    out += "    pub ";
                    out += toRustSignature(cleaned, /*isMember=*/true);
                    out += ";\n";
                }
            }
        }

        out += "}\n";
    }

    return out;
}

// ── Enum ─────────────────────────────────────────────────────────────────────

static std::string generateEnum(const PdbIndex& /*index*/, const SymbolNode& sym, bool prettify)
{
    const auto& en = std::get<EnumData>(sym.kindData);
    std::string out;

    auto name = symName(sym, prettify);
    auto underlying = rustTypeName(sym, prettify);

    if (!underlying.empty())
        out += std::format("#[repr({})]\n", underlying);
    else
        out += "#[repr(C)]\n";

    out += "pub enum ";
    out += name;
    out += " {\n";

    for (const auto& ev : en.enumValues) {
        out += std::format("    {} = {},\n", ev.name, ev.value);
    }

    out += "}\n";
    return out;
}

// ── Function ─────────────────────────────────────────────────────────────────

static std::string generateFunction(const PdbIndex& /*index*/, const SymbolNode& sym, bool prettify)
{
    auto cleaned = stripAccessSpecifiers(symName(sym, prettify));
    auto out = toRustSignature(cleaned, /*isMember=*/false);
    out += ";\n";
    return out;
}

// ── Data (constant) ──────────────────────────────────────────────────────────

static std::string generateData(const PdbIndex& /*index*/, const SymbolNode& sym, bool prettify)
{
    const auto& dat = std::get<DataSymData>(sym.kindData);
    if (!dat.hasConstValue) return {};

    auto tn = rustTypeName(sym, prettify);
    auto name = symName(sym, prettify);

    std::string out = "const ";
    out += name;
    if (!tn.empty()) {
        out += ": ";
        out += tn;
    }
    out += std::format(" = {};\n", dat.constValue);
    return out;
}

// ── public entry point ───────────────────────────────────────────────────────

std::string generateRust(const PdbIndex& index, const SymbolNode& sym, bool prettify,
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
