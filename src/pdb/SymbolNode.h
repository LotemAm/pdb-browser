#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

// Opaque ID type — index into PdbIndex::m_symbols.
using SymbolId = uint32_t;
using PdbId = uint32_t;
constexpr SymbolId INVALID_SYMBOL_ID = UINT32_MAX;

enum class SymbolKind : uint8_t {
    Unknown,
    Compiland,
    Function,
    PublicSymbol,
    UDT,        // struct / class / union
    Enum,
    Typedef,
    BaseType,
    PointerType,
    ArrayType,
    FunctionType,
    Data,       // global / static variable
};

constexpr inline const char* kindStr(SymbolKind k)
{
    switch (k) {
    case SymbolKind::Compiland:    return "compiland";
    case SymbolKind::Function:     return "function";
    case SymbolKind::PublicSymbol: return "public";
    case SymbolKind::UDT:          return "udt";
    case SymbolKind::Enum:         return "enum";
    case SymbolKind::Typedef:      return "typedef";
    case SymbolKind::Data:         return "data";
    default:                       return "unknown";
    }
}

// Calling conventions (mirrors CV_call_e)
enum class CallingConv : uint8_t {
    Unknown,
    CDecl,
    StdCall,
    FastCall,
    ThisCall,
    ClrCall,
    Other,
};

// Plain label (e.g. "cdecl") — used by export.
constexpr inline const char* callingConvStr(CallingConv cc)
{
    switch (cc) {
    case CallingConv::CDecl:    return "cdecl";
    case CallingConv::StdCall:  return "stdcall";
    case CallingConv::FastCall: return "fastcall";
    case CallingConv::ThisCall: return "thiscall";
    case CallingConv::ClrCall:  return "clrcall";
    case CallingConv::Other:    return "other";
    default:                    return "unknown";
    }
}

// Display label with __ prefix (e.g. "__cdecl") — used by UI.
constexpr inline const char* callingConvLabel(CallingConv cc)
{
    switch (cc) {
    case CallingConv::CDecl:    return "__cdecl";
    case CallingConv::StdCall:  return "__stdcall";
    case CallingConv::FastCall: return "__fastcall";
    case CallingConv::ThisCall: return "__thiscall";
    case CallingConv::ClrCall:  return "__clrcall";
    case CallingConv::Other:    return "(other)";
    default:                    return "";
    }
}

// UDT sub-kind (mirrors UdtTag from cvconst.h)
enum class UdtTag : uint8_t {
    Struct,
    Class,
    Union,
    Interface,
};

constexpr inline const char* udtTagStr(UdtTag k)
{
    switch (k) {
    case UdtTag::Struct:    return "Struct";
    case UdtTag::Class:     return "Class";
    case UdtTag::Union:     return "Union";
    case UdtTag::Interface: return "Interface";
    default:                 return "UDT";
    }
}

// ── UDT member ───────────────────────────────────────────────────────────────
struct MemberInfo {
    std::string name;
    std::string typeName;
    std::string prettyTypeName;          // prettified typeName (computed at load time)
    SymbolId    typeId{INVALID_SYMBOL_ID};
    uint32_t    offset{0};   // byte offset within parent UDT
    uint32_t    sizeBytes{0};
    uint8_t     bitOffset{0};
    uint8_t     bitSize{0};  // 0 = not a bitfield
};

// ── Enum enumerator ──────────────────────────────────────────────────────────
struct EnumValue {
    std::string name;
    int64_t     value{0};
};

// ── Template argument ───────────────────────────────────────────────────────
struct TemplateArg {
    std::string text;                       // full text (e.g., "int", "MyClass<T>")
    std::string prettyText;                 // prettified display text
    SymbolId    typeId{INVALID_SYMBOL_ID};  // resolved type symbol, if found
};

// ── Kind-specific data ──────────────────────────────────────────────────────

struct FunctionData {
    CallingConv callingConvention{CallingConv::Unknown};
    bool isInline{false};
    bool isVirtual{false};
    bool isPure{false};
    bool isStatic{false};
    bool isNoReturn{false};
};

struct CompilandData {
    std::vector<std::string> sourceFiles;   // source files contributed by this compiland
};

struct UdtData {
    UdtTag                   udtTag{UdtTag::Struct};
    bool                     isForwardRef{false};
    std::vector<MemberInfo>  members;
    std::vector<SymbolId>    baseClasses;   // parent UDT ids
    std::vector<SymbolId>    friends;       // friend class/function ids
};

struct EnumData {
    std::vector<EnumValue>   enumValues;
};

struct DataSymData {
    uint32_t    dataKind{0};                 // DIA DataKind (DataIsGlobal, DataIsConstant, etc.)
    int64_t     constValue{0};               // value for DataIsConstant symbols
    bool        hasConstValue{false};        // true when constValue is valid
};

// ── Canonical in-memory symbol ───────────────────────────────────────────────
// No live COM pointers — all data extracted during indexing.
struct SymbolNode {
    SymbolId    id{INVALID_SYMBOL_ID};
    SymbolKind  kind{SymbolKind::Unknown};

    PdbId       pdbId{0};                   // Id from the Pdb itself
    std::string name;
    std::string undecoratedName;
    std::string prettyName;              // prettified display name (computed at load time)

    // Location
    uint32_t rva{0};
    uint64_t sizeBytes{0};

    // Source info
    std::string sourceFile;
    uint32_t    sourceLine{0};

    // Kind-specific data
    std::variant<std::monostate, FunctionData, CompilandData, UdtData, EnumData, DataSymData> kindData;

    // Template arguments (parsed from name, resolved at load time) — used by Function + UDT
    std::vector<TemplateArg>  templateArgs;

    // Type linkage
    std::string typeName;                    // resolved type name (typedef target, enum underlying, data type)
    std::string prettyTypeName;              // prettified typeName (computed at load time)
    SymbolId    typeId{INVALID_SYMBOL_ID};   // return type (func) or value type (typedef/array)
    SymbolId    parentId{INVALID_SYMBOL_ID}; // owning UDT (for member functions)
    SymbolId    compilandId{INVALID_SYMBOL_ID}; // lexical parent compiland

    // Children (functions in compiland, members in UDT, etc.) — lazy loaded
    bool                   childrenLoaded{false};
    std::vector<SymbolId>  children;
};
