#include "PdbSession.h"
#include "Prettify.h"

// DIA SDK headers — Windows / MSVC only
#include <windows.h>
#include <atlbase.h>  // CComPtr, CComBSTR
#include <dia2.h>

#include <format>
#include <algorithm>

// ── helpers ──────────────────────────────────────────────────────────────────

static std::string bstrToUtf8(const CComBSTR& bstr)
{
    if (!bstr) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, bstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, bstr, -1, s.data(), len, nullptr, nullptr);
    return s;
}

static std::wstring utf8ToWstr(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring w(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

static SymbolKind diaTagToKind(DWORD tag)
{
    switch (tag) {
    case SymTagCompiland:       return SymbolKind::Compiland;
    case SymTagFunction:        return SymbolKind::Function;
    case SymTagPublicSymbol:    return SymbolKind::PublicSymbol;
    case SymTagUDT:             return SymbolKind::UDT;
    case SymTagEnum:            return SymbolKind::Enum;
    case SymTagTypedef:         return SymbolKind::Typedef;
    case SymTagBaseType:        return SymbolKind::BaseType;
    case SymTagPointerType:     return SymbolKind::PointerType;
    case SymTagArrayType:       return SymbolKind::ArrayType;
    case SymTagFunctionType:    return SymbolKind::FunctionType;
    case SymTagData:            return SymbolKind::Data;
    default:                    return SymbolKind::Unknown;
    }
}

// Build a human-readable C type name from a DIA type symbol.
// depth guards against pathological recursive pointer chains.
static std::string typeNameFromDia(IDiaSymbol* typeSym, int depth = 0)
{
    if (!typeSym || depth > 8) return "?";

    DWORD tag = 0;
    if (FAILED(typeSym->get_symTag(&tag))) return "?";

    switch (tag) {
    case SymTagUDT:
    case SymTagEnum:
    case SymTagTypedef: {
        CComBSTR name;
        if (typeSym->get_name(&name) == S_OK && name)
            return bstrToUtf8(name);
        return "?";
    }
    case SymTagPointerType: {
        BOOL isRef = FALSE;
        typeSym->get_reference(&isRef);
        CComPtr<IDiaSymbol> pointee;
        std::string inner = "void";
        if (typeSym->get_type(&pointee) == S_OK)
            inner = typeNameFromDia(pointee, depth + 1);
        return isRef ? inner + "&" : inner + "*";
    }
    case SymTagArrayType: {
        CComPtr<IDiaSymbol> elemType;
        if (typeSym->get_type(&elemType) == S_OK) {
            ULONGLONG arrLen = 0, elemLen = 1;
            typeSym->get_length(&arrLen);
            elemType->get_length(&elemLen);
            if (elemLen == 0) elemLen = 1;
            return std::format("{}[{}]", typeNameFromDia(elemType, depth + 1), arrLen / elemLen);
        }
        return "?[]";
    }
    case SymTagBaseType: {
        DWORD bt = 0;
        ULONGLONG len = 0;
        typeSym->get_baseType(&bt);
        typeSym->get_length(&len);
        switch (bt) {
        case btVoid:    return "void";
        case btChar:    return "char";
        case btWChar:   return "wchar_t";
        case btBool:    return "bool";
        case btLong:    return "long";
        case btULong:   return "unsigned long";
        case btInt:
            if (len == 1) return "int8_t";
            if (len == 2) return "int16_t";
            if (len == 4) return "int32_t";
            if (len == 8) return "int64_t";
            return "int";
        case btUInt:
            if (len == 1) return "uint8_t";
            if (len == 2) return "uint16_t";
            if (len == 4) return "uint32_t";
            if (len == 8) return "uint64_t";
            return "unsigned int";
        case btFloat:   return len == 4 ? "float" : "double";
        default:        return std::format("<base:{}>", bt);
        }
    }
    case SymTagFunctionType: return "<fn>";
    default: {
        CComBSTR name;
        if (typeSym->get_name(&name) == S_OK && name)
            return bstrToUtf8(name);
        return "?";
    }
    }
}

// Follow pointer/reference/array chains to find the inner named type
// (UDT, Enum, Typedef). Returns null if the leaf is a base/function type.
static CComPtr<IDiaSymbol> unwrapToNamedType(IDiaSymbol* typeSym, int depth = 0)
{
    if (!typeSym || depth > 8) return nullptr;
    DWORD tag = 0;
    if (FAILED(typeSym->get_symTag(&tag))) return nullptr;
    switch (tag) {
    case SymTagUDT:
    case SymTagEnum:
    case SymTagTypedef:
        return CComPtr<IDiaSymbol>(typeSym);
    case SymTagPointerType:
    case SymTagArrayType: {
        CComPtr<IDiaSymbol> inner;
        if (typeSym->get_type(&inner) == S_OK)
            return unwrapToNamedType(inner, depth + 1);
        return nullptr;
    }
    default:
        return nullptr;
    }
}

// ── Template argument parsing ───────────────────────────────────────────────

// Return the last qualified-name segment (after the last :: at depth 0).
// E.g. "std::vector<int>::push_back" → "push_back"
//      "std::vector<int,std::allocator<int>>" → "vector<int,std::allocator<int>>"
static std::string_view lastNameSegment(std::string_view name)
{
    int depth = 0;
    size_t lastSep = 0;
    bool found = false;
    for (size_t i = 0; i + 1 < name.size(); ++i) {
        if (name[i] == '<') ++depth;
        else if (name[i] == '>') --depth;
        else if (depth == 0 && name[i] == ':' && name[i + 1] == ':') {
            lastSep = i + 2;
            found = true;
        }
    }
    return found ? name.substr(lastSep) : name;
}

// Parse the template argument list from a (possibly qualified) name.
// Uses only the last name segment so that parent-class template args
// are not attributed to a member function.
static std::vector<std::string> parseTemplateArgs(std::string_view name)
{
    auto segment = lastNameSegment(name);
    auto openPos = segment.find('<');
    if (openPos == std::string_view::npos) return {};

    // Guard against operator<, operator<<, operator<=> etc.
    {
        auto before = segment.substr(0, openPos);
        while (!before.empty() && before.back() == ' ') before.remove_suffix(1);
        if (before.size() >= 8 && before.substr(before.size() - 8) == std::string_view("operator"))
            return {};
    }

    std::vector<std::string> args;
    int depth = 0;
    size_t argStart = openPos + 1;
    for (size_t i = openPos; i < segment.size(); ++i) {
        char c = segment[i];
        if (c == '<') {
            ++depth;
        } else if (c == '>') {
            --depth;
            if (depth == 0) {
                auto arg = segment.substr(argStart, i - argStart);
                auto s = arg.find_first_not_of(' ');
                auto e = arg.find_last_not_of(' ');
                if (s != std::string_view::npos)
                    args.emplace_back(arg.substr(s, e - s + 1));
                break;
            }
        } else if (c == ',' && depth == 1) {
            auto arg = segment.substr(argStart, i - argStart);
            auto s = arg.find_first_not_of(' ');
            auto e = arg.find_last_not_of(' ');
            if (s != std::string_view::npos)
                args.emplace_back(arg.substr(s, e - s + 1));
            argStart = i + 1;
        }
    }

    return args;
}

// Strip pointer / reference / cv-qualifiers to get the core type name for lookup.
static std::string coreTypeName(std::string_view s)
{
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ')  s.remove_suffix(1);

    // Strip leading qualifiers
    for (std::string_view prefix : {"const ", "volatile ", "struct ", "class ", "enum ", "union "}) {
        if (s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix) {
            s.remove_prefix(prefix.size());
            break;
        }
    }

    // Strip trailing * & and cv-qualifiers
    bool changed = true;
    while (changed) {
        changed = false;
        while (!s.empty() && s.back() == ' ') { s.remove_suffix(1); changed = true; }
        if (!s.empty() && (s.back() == '*' || s.back() == '&')) {
            s.remove_suffix(1); changed = true;
        }
        if (s.ends_with(" const"))    { s.remove_suffix(6); changed = true; }
        if (s.ends_with(" volatile")) { s.remove_suffix(9); changed = true; }
    }
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);

    return std::string(s);
}

// Parse template args from a symbol name and resolve each to a SymbolId.
static std::vector<TemplateArg> resolveTemplateArgs(const std::string& name, PdbIndex& index)
{
    auto argTexts = parseTemplateArgs(name);
    std::vector<TemplateArg> result;
    result.reserve(argTexts.size());

    for (auto& text : argTexts) {
        TemplateArg ta;
        ta.text = std::move(text);
        ta.prettyText = prettifyName(ta.text);

        // Try exact name match
        auto hits = index.findByName(ta.text);
        if (!hits.empty()) {
            ta.typeId = hits[0];
        } else {
            // Try stripping pointer/ref/cv-qual decorators
            auto core = coreTypeName(ta.text);
            if (!core.empty() && core != ta.text) {
                hits = index.findByName(core);
                if (!hits.empty())
                    ta.typeId = hits[0];
            }
        }

        result.push_back(std::move(ta));
    }
    return result;
}

// ── PdbSession ───────────────────────────────────────────────────────────────

PdbSession::PdbSession()
{
    // DIA requires STA on each thread that uses it
    CoInitialize(nullptr);
}

PdbSession::~PdbSession()
{
    CoUninitialize();
}

std::expected<std::shared_ptr<PdbIndex>, std::string>
PdbSession::load(const std::string& path)
{
    HRESULT hr = CoCreateInstance(
        CLSID_DiaSource, nullptr, CLSCTX_INPROC_SERVER,
        IID_IDiaDataSource, reinterpret_cast<void**>(&m_source));
    if (FAILED(hr))
        return std::unexpected("CoCreateInstance(DiaSource) failed — is msdia140.dll registered?");

    std::wstring wpath = utf8ToWstr(path);
    hr = m_source->loadDataFromPdb(wpath.c_str());
    if (FAILED(hr))
        return std::unexpected(std::format("loadDataFromPdb failed for: {}", path));

    hr = m_source->openSession(&m_session);
    if (FAILED(hr))
        return std::unexpected("openSession failed");

    auto index = std::make_shared<PdbIndex>();
    index->setPdbPath(path);

    // Pre-size the index based on global scope symbol count to avoid
    // repeated reallocations of the large SymbolNode vector.
    {
        CComPtr<IDiaSymbol> global;
        if (m_session->get_globalScope(&global) == S_OK) {
            CComPtr<IDiaEnumSymbols> allSyms;
            if (global->findChildren(SymTagNull, nullptr, nsNone, &allSyms) == S_OK) {
                LONG count = 0;
                if (allSyms->get_Count(&count) == S_OK && count > 0)
                    index->reserve(static_cast<size_t>(count));
            }
        }
    }

    indexCompilands(m_session, *index);
    indexTypes(m_session, *index);
    indexGlobals(m_session, *index);

    return index;
}

std::expected<IDiaSymbol*, std::string> getDiaSymbol(IDiaSession* session, const SymbolNode* node)
{
    IDiaSymbol* ppSymbol;
    if (node->pdbId != 0 && session->symbolById(node->pdbId, &ppSymbol) == S_OK) {
        return ppSymbol;
    }
    else if (node->rva != 0 && session->findSymbolByRVA(node->rva, SymTagNull, &ppSymbol) == S_OK) {
        return ppSymbol;
    }
    else {
        return std::unexpected(std::format("findSymbolByRVA failed for RVA = {:08X}", node->rva));
    }
}

std::expected<SymbolNode, std::string> PdbSession::loadSymbolData(std::shared_ptr<PdbIndex> activeIndex, SymbolId id)
{
    auto* index = activeIndex.get();
    auto* sym = index->getSymbol(id);

	auto diaSymOrError = getDiaSymbol(m_session, sym);

    return diaSymOrError.and_then([&](IDiaSymbol* diaSym) -> std::expected<SymbolNode, std::string> {
        using enum SymbolKind;
        switch (sym->kind)
        {
        case Compiland:
            getCompilandSymbolData(id, diaSym, *index);
            break;
        case Function:
            getFunctionSymbolData(id, diaSym, *index);
            break;
        case UDT:
            getUDTSymbolData(id, diaSym, *index);
            break;
        case Enum:
            getEnumSymbolData(id, diaSym, *index);
            break;
        case Typedef:
            getTypedefSymbolData(id, diaSym, *index);
            break;
        case Data:
            getDataSymbolData(id, diaSym, *index);
            break;
        }

        return {};
	});
}

void PdbSession::resolveAllCompilandIds(PdbIndex& index, std::atomic<int>& progress, std::atomic<bool>& cancel)
{
    // Snapshot the count — only resolve symbols that existed before we started.
    // resolveLexicalCompiland may add new compiland symbols via registerCompiland,
    // but those are compilands themselves and don't need resolution.
    const size_t total = index.symbolCount();
    for (size_t i = 0; i < total; ++i) {
        if (cancel.load()) break;

        // Fetch the symbol — note: pointer may be invalidated by addSymbol below,
        // so we read kind/compilandId/pdbId/rva into locals before calling DIA.
        auto* sym = index.getSymbol(static_cast<SymbolId>(i));
        if (!sym || sym->compilandId != INVALID_SYMBOL_ID) {
            progress.store(static_cast<int>(i + 1));
            continue;
        }

        // Skip compilands themselves and internal types
        if (sym->kind == SymbolKind::Compiland ||
            sym->kind == SymbolKind::BaseType ||
            sym->kind == SymbolKind::PointerType ||
            sym->kind == SymbolKind::ArrayType ||
            sym->kind == SymbolKind::FunctionType ||
            sym->kind == SymbolKind::Unknown)
        {
            progress.store(static_cast<int>(i + 1));
            continue;
        }

        // Copy fields needed for DIA lookup before any potential reallocation
        PdbId pdbId = sym->pdbId;
        uint32_t rva = sym->rva;

        // Look up DIA symbol
        CComPtr<IDiaSymbol> diaSym;
        if (pdbId != 0 && m_session->symbolById(pdbId, &diaSym) == S_OK && diaSym) {
            SymbolId compId = resolveLexicalCompiland(diaSym, index);
            // Re-fetch pointer — index may have grown via registerCompiland
            auto* node = index.getSymbol(static_cast<SymbolId>(i));
            if (node) node->compilandId = compId;
        } else if (rva != 0) {
            IDiaSymbol* pRaw = nullptr;
            if (m_session->findSymbolByRVA(rva, SymTagNull, &pRaw) == S_OK && pRaw) {
                CComPtr<IDiaSymbol> diaSym2(pRaw);
                SymbolId compId = resolveLexicalCompiland(diaSym2, index);
                auto* node = index.getSymbol(static_cast<SymbolId>(i));
                if (node) node->compilandId = compId;
            }
        }

        progress.store(static_cast<int>(i + 1));
    }
}

static int64_t variantToInt64(const VARIANT& v);

// Add a DIA type symbol to the index.  If it's a pointer or array, also add
// intermediate types along the chain so that resolveNavigable() in the UI can
// walk typeId links from pointer → pointee → … → UDT/Enum/Typedef.
SymbolId PdbSession::addTypeChainToIndex(IDiaSymbol* typeSym, PdbIndex& index, int depth)
{
    if (!typeSym || depth > 8) return INVALID_SYMBOL_ID;

    DWORD pdbId = 0;
    typeSym->get_symIndexId(&pdbId);
    SymbolId existing = index.findByPdbId(pdbId);
    if (existing != INVALID_SYMBOL_ID) return existing;

    DWORD tag = 0;
    typeSym->get_symTag(&tag);
    SymbolKind kind = diaTagToKind(tag);

    SymbolId id = index.addSymbol(extractSymbol(typeSym, kind));

    if (kind == SymbolKind::PointerType || kind == SymbolKind::ArrayType) {
        CComPtr<IDiaSymbol> inner;
        if (typeSym->get_type(&inner) == S_OK && inner) {
            SymbolId innerId = addTypeChainToIndex(inner, index, depth + 1);
            auto* node = index.getSymbol(id);
            if (node)
                node->typeId = innerId;
        }
    }

    return id;
}

SymbolNode PdbSession::extractSymbol(IDiaSymbol* sym, SymbolKind kind)
{
    SymbolNode node;
    node.kind = kind;

    DWORD internalId = 0;
    if (sym->get_symIndexId(&internalId) == S_OK)
        node.pdbId = internalId;

    CComBSTR name;
    if (sym->get_name(&name) == S_OK)
        node.name = bstrToUtf8(name);

    CComBSTR undecName;
    if (sym->get_undecoratedName(&undecName) == S_OK)
        node.undecoratedName = bstrToUtf8(undecName);

    // Pre-compute prettified display name
    const std::string& bestName = node.undecoratedName.empty() ? node.name : node.undecoratedName;
    if (!bestName.empty())
        node.prettyName = prettifyName(bestName);

    DWORD rva = 0;
    if (sym->get_relativeVirtualAddress(&rva) == S_OK)
        node.rva = rva;

    ULONGLONG size = 0;
    if (sym->get_length(&size) == S_OK)
        node.sizeBytes = size;

    if (kind == SymbolKind::Function) {
        FunctionData fd;
        BOOL isInline = FALSE;
        sym->get_inlSpec(&isInline);
        fd.isInline = isInline != 0;

        BOOL isVirtual = FALSE;
        sym->get_virtual(&isVirtual);
        fd.isVirtual = isVirtual != 0;

        BOOL isPure = FALSE;
        sym->get_pure(&isPure);
        fd.isPure = isPure != 0;

        BOOL isStatic = FALSE;
        sym->get_isStatic(&isStatic);
        fd.isStatic = isStatic != 0;

        node.kindData = std::move(fd);
    }

    if (kind == SymbolKind::Compiland) {
        node.kindData = CompilandData{};
    }

    if (kind == SymbolKind::Data) {
        DataSymData dd;
        DWORD dk = 0;
        if (sym->get_dataKind(&dk) == S_OK)
            dd.dataKind = dk;

        if (dk == DataIsConstant) {
            VARIANT val;
            VariantInit(&val);
            if (sym->get_value(&val) == S_OK) {
                dd.constValue = variantToInt64(val);
                dd.hasConstValue = true;
            }
            VariantClear(&val);
        }
        node.kindData = std::move(dd);
    }

    if (kind == SymbolKind::UDT) {
        UdtData ud;
        BOOL fwdRef = FALSE;
		sym->get_exportIsForwarder(&fwdRef);
        //sym->get_isForwardDecl(&fwdRef);
        ud.isForwardRef = fwdRef != 0;

        DWORD uk = 0;
        if (sym->get_udtKind(&uk) == S_OK) {
            switch (uk) {
            case UdtStruct:    ud.udtTag = UdtTag::Struct;    break;
            case UdtClass:     ud.udtTag = UdtTag::Class;     break;
            case UdtUnion:     ud.udtTag = UdtTag::Union;     break;
            case UdtInterface: ud.udtTag = UdtTag::Interface; break;
            }
        }
        node.kindData = std::move(ud);
    }

    if (kind == SymbolKind::Enum) {
        node.kindData = EnumData{};
    }

    return node;
}

// Register a compiland DIA symbol in the index, returning its SymbolId.
SymbolId PdbSession::registerCompiland(IDiaSymbol* compilandSym, PdbIndex& index)
{
    DWORD pdbId = 0;
    compilandSym->get_symIndexId(&pdbId);
    SymbolId id = index.findByPdbId(pdbId);
    if (id == INVALID_SYMBOL_ID)
        id = index.addSymbol(extractSymbol(compilandSym, SymbolKind::Compiland));
    return id;
}

// Find the compiland that contains the given RVA via line-number info.
SymbolId PdbSession::findCompilandByRVA(DWORD rva, PdbIndex& index)
{
    if (!m_session || rva == 0) return INVALID_SYMBOL_ID;
    CComPtr<IDiaEnumLineNumbers> pLines;
    if (m_session->findLinesByRVA(rva, 1, &pLines) != S_OK || !pLines)
        return INVALID_SYMBOL_ID;
    CComPtr<IDiaLineNumber> pLine;
    ULONG fetched = 0;
    if (pLines->Next(1, &pLine, &fetched) != S_OK || fetched != 1)
        return INVALID_SYMBOL_ID;
    CComPtr<IDiaSymbol> compilandSym;
    if (pLine->get_compiland(&compilandSym) != S_OK || !compilandSym)
        return INVALID_SYMBOL_ID;
    return registerCompiland(compilandSym, index);
}

// Resolve the compiland that a DIA symbol belongs to.  Uses multiple
// strategies because global-scope types don't have a compiland as their
// lexical parent.
SymbolId PdbSession::resolveLexicalCompiland(IDiaSymbol* diaSym, PdbIndex& index)
{
    // ── Strategy 1: walk the lexical-parent chain ────────────────────────────
    // Works for functions, data, and symbols nested inside a compiland.
    {
        CComPtr<IDiaSymbol> current;
        if (diaSym->get_lexicalParent(&current) == S_OK && current) {
            for (int depth = 0; depth < 10 && current; ++depth) {
                DWORD tag = 0;
                current->get_symTag(&tag);
                if (tag == SymTagCompiland)
                    return registerCompiland(current, index);
                if (tag == SymTagExe)
                    break;                          // reached global scope
                CComPtr<IDiaSymbol> parent;
                if (current->get_lexicalParent(&parent) != S_OK || !parent)
                    break;
                current = parent;
            }
        }
    }

    // ── Strategy 2: use the symbol's own RVA ─────────────────────────────────
    // Data / functions that somehow didn't resolve via the chain.
    {
        DWORD rva = 0;
        if (diaSym->get_relativeVirtualAddress(&rva) == S_OK && rva != 0) {
            SymbolId id = findCompilandByRVA(rva, index);
            if (id != INVALID_SYMBOL_ID) return id;
        }
    }

    // ── Strategy 3: for UDTs, find the first member function with an RVA ────
    // Global-scope types have no compiland parent, but their member functions
    // are compiled inside a specific compiland.
    {
        DWORD tag = 0;
        diaSym->get_symTag(&tag);
        if (tag == SymTagUDT) {
            CComPtr<IDiaEnumSymbols> funcs;
            if (diaSym->findChildren(SymTagFunction, nullptr, nsNone, &funcs) == S_OK && funcs) {
                IDiaSymbol* pRaw = nullptr;
                ULONG celt = 0;
                while (funcs->Next(1, &pRaw, &celt) == S_OK && celt == 1) {
                    CComPtr<IDiaSymbol> func(pRaw);
                    DWORD funcRva = 0;
                    if (func->get_relativeVirtualAddress(&funcRva) == S_OK && funcRva != 0) {
                        SymbolId id = findCompilandByRVA(funcRva, index);
                        if (id != INVALID_SYMBOL_ID) return id;
                    }
                }
            }
        }
    }

    return INVALID_SYMBOL_ID;
}

void PdbSession::indexCompilands(IDiaSession* session, PdbIndex& index)
{
    CComPtr<IDiaSymbol> global;
    if (FAILED(session->get_globalScope(&global))) return;

    CComPtr<IDiaEnumSymbols> compilands;
    if (FAILED(global->findChildren(SymTagCompiland, nullptr, nsNone, &compilands))) return;

    IDiaSymbol* pCompiland = nullptr;
    ULONG fetched = 0;
    while (compilands->Next(1, &pCompiland, &fetched) == S_OK && fetched == 1) {
        CComPtr<IDiaSymbol> compiland(pCompiland);

        SymbolNode node = extractSymbol(compiland, SymbolKind::Compiland);
        index.addSymbol(std::move(node));
    }
}

void PdbSession::indexTypes(IDiaSession* session, PdbIndex& index)
{
    CComPtr<IDiaSymbol> global;
    if (FAILED(session->get_globalScope(&global))) return;

    // UDTs
    {
        CComPtr<IDiaEnumSymbols> syms;
        if (global->findChildren(SymTagUDT, nullptr, nsNone, &syms) == S_OK) {
            IDiaSymbol* pSym = nullptr;
            ULONG fetched = 0;
            while (syms->Next(1, &pSym, &fetched) == S_OK && fetched == 1) {
                CComPtr<IDiaSymbol> sym(pSym);
                SymbolNode node = extractSymbol(sym, SymbolKind::UDT);
                if (!std::get<UdtData>(node.kindData).isForwardRef)
                    index.addSymbol(std::move(node));
            }
        }
    }

    // Enums
    {
        CComPtr<IDiaEnumSymbols> syms;
        if (global->findChildren(SymTagEnum, nullptr, nsNone, &syms) == S_OK) {
            IDiaSymbol* pSym = nullptr;
            ULONG fetched = 0;
            while (syms->Next(1, &pSym, &fetched) == S_OK && fetched == 1) {
                CComPtr<IDiaSymbol> sym(pSym);
                SymbolNode node = extractSymbol(sym, SymbolKind::Enum);
                index.addSymbol(std::move(node));
            }
        }
    }

    // Typedefs
    {
        CComPtr<IDiaEnumSymbols> syms;
        if (global->findChildren(SymTagTypedef, nullptr, nsNone, &syms) == S_OK) {
            IDiaSymbol* pSym = nullptr;
            ULONG fetched = 0;
            while (syms->Next(1, &pSym, &fetched) == S_OK && fetched == 1) {
                CComPtr<IDiaSymbol> sym(pSym);
                SymbolNode node = extractSymbol(sym, SymbolKind::Typedef);
                index.addSymbol(std::move(node));
            }
        }
    }
}

void PdbSession::indexGlobals(IDiaSession* session, PdbIndex& index)
{
    CComPtr<IDiaSymbol> global;
    if (FAILED(session->get_globalScope(&global))) return;

    // Global data variables
    {
        CComPtr<IDiaEnumSymbols> syms;
        if (global->findChildren(SymTagData, nullptr, nsNone, &syms) == S_OK) {
            IDiaSymbol* pSym = nullptr;
            ULONG fetched = 0;
            while (syms->Next(1, &pSym, &fetched) == S_OK && fetched == 1) {
                CComPtr<IDiaSymbol> sym(pSym);
                SymbolNode node = extractSymbol(sym, SymbolKind::Data);
                index.addSymbol(std::move(node));
            }
        }
    }

    // Global functions
    {
        CComPtr<IDiaEnumSymbols> syms;
        if (global->findChildren(SymTagFunction, nullptr, nsNone, &syms) == S_OK) {
            IDiaSymbol* pSym = nullptr;
            ULONG fetched = 0;
            while (syms->Next(1, &pSym, &fetched) == S_OK && fetched == 1) {
                CComPtr<IDiaSymbol> sym(pSym);
                SymbolNode node = extractSymbol(sym, SymbolKind::Function);
                index.addSymbol(std::move(node));
            }
        }
    }
}

void PdbSession::getCompilandSymbolData(SymbolId targetId, IDiaSymbol* diaSym, PdbIndex& index)
{
    // Collect into locals — addSymbol can reallocate m_symbols.
    std::vector<SymbolId>    children;
    std::vector<std::string> sourceFiles;
    std::string              libraryName;

    // ── Library name ─────────────────────────────────────────────────────────
    {
        CComBSTR libName;
        if (diaSym->get_libraryName(&libName) == S_OK && libName)
            libraryName = bstrToUtf8(libName);
    }

    // ── Source files ─────────────────────────────────────────────────────────
    if (m_session) {
        CComPtr<IDiaEnumSourceFiles> pFiles;
        if (m_session->findFile(diaSym, nullptr, nsNone, &pFiles) == S_OK && pFiles) {
            IDiaSourceFile* pRaw = nullptr;
            ULONG celt = 0;
            while (pFiles->Next(1, &pRaw, &celt) == S_OK && celt == 1) {
                CComPtr<IDiaSourceFile> pSrcFile(pRaw);
                CComBSTR fileName;
                if (pSrcFile->get_fileName(&fileName) == S_OK && fileName)
                    sourceFiles.push_back(bstrToUtf8(fileName));
            }
        }
    }

    // ── Child functions ──────────────────────────────────────────────────────
    {
        CComPtr<IDiaEnumSymbols> ppEnum;
        if (diaSym->findChildren(SymTagFunction, nullptr, nsNone, &ppEnum) == S_OK) {
            IDiaSymbol* pRaw = nullptr;
            ULONG celt = 0;
            while (ppEnum->Next(1, &pRaw, &celt) == S_OK && celt == 1) {
                CComPtr<IDiaSymbol> pFunc(pRaw);
                DWORD funcSymId = 0;
                pFunc->get_symIndexId(&funcSymId);
                SymbolId funcId = index.findByPdbId(funcSymId);
                if (funcId == INVALID_SYMBOL_ID)
                    funcId = index.addSymbol(extractSymbol(pFunc, SymbolKind::Function));
                children.push_back(funcId);
            }
        }
    }

    // ── Child data (globals/statics defined in this compiland) ────────────────
    {
        CComPtr<IDiaEnumSymbols> ppEnum;
        if (diaSym->findChildren(SymTagData, nullptr, nsNone, &ppEnum) == S_OK) {
            IDiaSymbol* pRaw = nullptr;
            ULONG celt = 0;
            while (ppEnum->Next(1, &pRaw, &celt) == S_OK && celt == 1) {
                CComPtr<IDiaSymbol> pData(pRaw);
                DWORD dataSymId = 0;
                pData->get_symIndexId(&dataSymId);
                SymbolId dataId = index.findByPdbId(dataSymId);
                if (dataId == INVALID_SYMBOL_ID)
                    dataId = index.addSymbol(extractSymbol(pData, SymbolKind::Data));
                children.push_back(dataId);
            }
        }
    }

    // ── Write back ───────────────────────────────────────────────────────────
    auto* node = index.getSymbol(targetId);
    if (node) {
        node->children       = std::move(children);
        std::get<CompilandData>(node->kindData).sourceFiles = std::move(sourceFiles);
        node->typeName       = std::move(libraryName);
        node->childrenLoaded = true;
    }
}

static CallingConv diaCallingConv(DWORD cv)
{
    switch (cv) {
    case CV_CALL_NEAR_C:
    case CV_CALL_FAR_C:       return CallingConv::CDecl;
    case CV_CALL_NEAR_STD:
    case CV_CALL_FAR_STD:     return CallingConv::StdCall;
    case CV_CALL_NEAR_FAST:
    case CV_CALL_FAR_FAST:    return CallingConv::FastCall;
    case CV_CALL_THISCALL:    return CallingConv::ThisCall;
    case CV_CALL_CLRCALL:     return CallingConv::ClrCall;
    default:                  return CallingConv::Other;
    }
}

void PdbSession::getFunctionSymbolData(SymbolId targetId, IDiaSymbol* diaSym, PdbIndex& index)
{
    // All data collected into locals — no addSymbol here, but keeping pattern consistent
    std::string sourceFile;
    uint32_t    sourceLine = 0;
    CallingConv callingConv = CallingConv::Unknown;
    std::string returnTypeName;
    SymbolId    returnTypeId = INVALID_SYMBOL_ID;
    bool        isNoReturn = false;
    SymbolId    parentId = INVALID_SYMBOL_ID;

    // ── Calling convention ───────────────────────────────────────────────────
    CComPtr<IDiaSymbol> funcType;
    if (diaSym->get_type(&funcType) == S_OK && funcType) {
        DWORD cv = 0;
        if (funcType->get_callingConvention(&cv) == S_OK)
            callingConv = diaCallingConv(cv);

        // Return type
        CComPtr<IDiaSymbol> retType;
        if (funcType->get_type(&retType) == S_OK && retType) {
            returnTypeName = typeNameFromDia(retType);

            CComPtr<IDiaSymbol> namedRet = unwrapToNamedType(retType);
            IDiaSymbol* retToRegister = namedRet ? namedRet : retType;
            DWORD retSymId = 0;
            if (retToRegister->get_symIndexId(&retSymId) == S_OK) {
                returnTypeId = index.findByPdbId(retSymId);
                if (returnTypeId == INVALID_SYMBOL_ID) {
                    DWORD retTag = 0;
                    retToRegister->get_symTag(&retTag);
                    returnTypeId = index.addSymbol(extractSymbol(retToRegister, diaTagToKind(retTag)));
                }
            }
        }
    }

    // ── No-return ────────────────────────────────────────────────────────────
    BOOL noRet = FALSE;
    if (diaSym->get_noReturn(&noRet) == S_OK)
        isNoReturn = noRet != 0;

    // ── Source file + line ────────────────────────────────────────────────────
    {
        DWORD rva = 0;
        diaSym->get_relativeVirtualAddress(&rva);
        if (rva != 0 && m_session) {
            CComPtr<IDiaEnumLineNumbers> pLines;
            if (m_session->findLinesByRVA(rva, 1, &pLines) == S_OK && pLines) {
                CComPtr<IDiaLineNumber> pLine;
                ULONG fetched = 0;
                if (pLines->Next(1, &pLine, &fetched) == S_OK && fetched == 1) {
                    DWORD line = 0;
                    if (pLine->get_lineNumber(&line) == S_OK)
                        sourceLine = line;

                    CComPtr<IDiaSourceFile> pSrcFile;
                    if (pLine->get_sourceFile(&pSrcFile) == S_OK && pSrcFile) {
                        CComBSTR fileName;
                        if (pSrcFile->get_fileName(&fileName) == S_OK)
                            sourceFile = bstrToUtf8(fileName);
                    }
                }
            }
        }
    }

    // ── Parent class (if member function) ────────────────────────────────────
    {
        CComPtr<IDiaSymbol> pParent;
        if (diaSym->get_classParent(&pParent) == S_OK && pParent) {
            DWORD parentSymId = 0;
            if (pParent->get_symIndexId(&parentSymId) == S_OK) {
                parentId = index.findByPdbId(parentSymId);
                if (parentId == INVALID_SYMBOL_ID)
                    parentId = index.addSymbol(extractSymbol(pParent, SymbolKind::UDT));
            }
        }
    }

    // ── Compiland (lexical parent) ──────────────────────────────────────────
    SymbolId compilandId = resolveLexicalCompiland(diaSym, index);

    // ── Write back ───────────────────────────────────────────────────────────
    auto* node = index.getSymbol(targetId);
    if (node) {
        node->sourceFile       = std::move(sourceFile);
        node->sourceLine       = sourceLine;
        auto& fn               = std::get<FunctionData>(node->kindData);
        fn.callingConvention   = callingConv;
        fn.isNoReturn          = isNoReturn;
        node->typeName         = std::move(returnTypeName);
        node->prettyTypeName   = prettifyName(node->typeName);
        node->typeId           = returnTypeId;
        node->parentId         = parentId;
        node->compilandId      = compilandId;
        node->templateArgs     = resolveTemplateArgs(node->name, index);
        node->childrenLoaded   = true;
    }
}

void PdbSession::getUDTSymbolData(SymbolId targetId, IDiaSymbol* diaSym, PdbIndex& index)
{
    // Collect into locals first — index.addSymbol() can reallocate m_symbols,
    // so we must not hold a SymbolNode* across any addSymbol call.
    std::vector<MemberInfo> members;
    std::vector<SymbolId>   baseClasses;
    std::vector<SymbolId>   children;
    std::vector<SymbolId>   friends;

    // ── 1. Data members ──────────────────────────────────────────────────────
    {
        CComPtr<IDiaEnumSymbols> ppEnum;
        if (diaSym->findChildren(SymTagData, nullptr, nsNone, &ppEnum) == S_OK) {
            IDiaSymbol* pRaw = nullptr;
            ULONG celt = 0;
            while (ppEnum->Next(1, &pRaw, &celt) == S_OK && celt == 1) {
                CComPtr<IDiaSymbol> pMember(pRaw);

                // Skip statics — only instance data members
                DWORD dataKind = 0;
                pMember->get_dataKind(&dataKind);
                if (dataKind != DataIsMember)
                    continue;

                MemberInfo mi;

                CComBSTR mName;
                if (pMember->get_name(&mName) == S_OK)
                    mi.name = bstrToUtf8(mName);

                LONG offset = 0;
                if (pMember->get_offset(&offset) == S_OK)
                    mi.offset = static_cast<uint32_t>(offset);

                DWORD locType = 0;
                pMember->get_locationType(&locType);
                if (locType == LocIsBitField) {
                    DWORD bitPos = 0;
                    pMember->get_bitPosition(&bitPos);
                    mi.bitOffset = static_cast<uint8_t>(bitPos);
                    ULONGLONG bitLen = 0;
                    pMember->get_length(&bitLen);
                    mi.bitSize = static_cast<uint8_t>(bitLen);
                } else {
                    ULONGLONG len = 0;
                    if (pMember->get_length(&len) == S_OK)
                        mi.sizeBytes = static_cast<uint32_t>(len);
                }

                CComPtr<IDiaSymbol> pType;
                if (pMember->get_type(&pType) == S_OK) {
                    mi.typeName = typeNameFromDia(pType);
                    mi.prettyTypeName = prettifyName(mi.typeName);

                    if (mi.sizeBytes == 0) {
                        ULONGLONG len = 0;
                        if (pType->get_length(&len) == S_OK)
                            mi.sizeBytes = static_cast<uint32_t>(len);
                    }

                    // Try to unwrap pointers/arrays to find the inner named type
                    // so that e.g. "SomeStruct*" links to SomeStruct
                    CComPtr<IDiaSymbol> namedType = unwrapToNamedType(pType);
                    IDiaSymbol* typeToRegister = namedType ? namedType : pType;

                    DWORD typeSymId = 0;
                    if (typeToRegister->get_symIndexId(&typeSymId) == S_OK) {
                        mi.typeId = index.findByPdbId(typeSymId);
                        if (mi.typeId == INVALID_SYMBOL_ID) {
                            DWORD typeTag = 0;
                            typeToRegister->get_symTag(&typeTag);
                            mi.typeId = index.addSymbol(extractSymbol(typeToRegister, diaTagToKind(typeTag)));
                        }
                    }
                }

                members.push_back(std::move(mi));
            }
        }
    }
    std::ranges::sort(members, {}, &MemberInfo::offset);

    // ── 2. Base classes ───────────────────────────────────────────────────────
    {
        CComPtr<IDiaEnumSymbols> ppEnum;
        if (diaSym->findChildren(SymTagBaseClass, nullptr, nsNone, &ppEnum) == S_OK) {
            IDiaSymbol* pRaw = nullptr;
            ULONG celt = 0;
            while (ppEnum->Next(1, &pRaw, &celt) == S_OK && celt == 1) {
                CComPtr<IDiaSymbol> pBase(pRaw);
                CComPtr<IDiaSymbol> pBaseUDT;
                if (pBase->get_type(&pBaseUDT) == S_OK) {
                    DWORD baseSymId = 0;
                    pBaseUDT->get_symIndexId(&baseSymId);
                    SymbolId baseId = index.findByPdbId(baseSymId);
                    if (baseId == INVALID_SYMBOL_ID)
                        baseId = index.addSymbol(extractSymbol(pBaseUDT, SymbolKind::UDT));
                    baseClasses.push_back(baseId);
                }
            }
        }
    }

    // ── 3. Member functions ───────────────────────────────────────────────────
    {
        CComPtr<IDiaEnumSymbols> ppEnum;
        if (diaSym->findChildren(SymTagFunction, nullptr, nsNone, &ppEnum) == S_OK) {
            IDiaSymbol* pRaw = nullptr;
            ULONG celt = 0;
            while (ppEnum->Next(1, &pRaw, &celt) == S_OK && celt == 1) {
                CComPtr<IDiaSymbol> pFunc(pRaw);
                DWORD funcSymId = 0;
                pFunc->get_symIndexId(&funcSymId);
                SymbolId funcId = index.findByPdbId(funcSymId);
                if (funcId == INVALID_SYMBOL_ID)
                    funcId = index.addSymbol(extractSymbol(pFunc, SymbolKind::Function));
                children.push_back(funcId);
            }
        }
    }

    // ── 4. Static data members ─────────────────────────────────────────────────
    {
        CComPtr<IDiaEnumSymbols> ppEnum;
        if (diaSym->findChildren(SymTagData, nullptr, nsNone, &ppEnum) == S_OK) {
            IDiaSymbol* pRaw = nullptr;
            ULONG celt = 0;
            while (ppEnum->Next(1, &pRaw, &celt) == S_OK && celt == 1) {
                CComPtr<IDiaSymbol> pData(pRaw);
                DWORD dataKind = 0;
                pData->get_dataKind(&dataKind);
                if (dataKind != DataIsStaticMember)
                    continue;
                DWORD dataSymId = 0;
                pData->get_symIndexId(&dataSymId);
                SymbolId dataId = index.findByPdbId(dataSymId);
                if (dataId == INVALID_SYMBOL_ID)
                    dataId = index.addSymbol(extractSymbol(pData, SymbolKind::Data));
                children.push_back(dataId);
            }
        }
    }

    // ── 5. Friend classes/functions ─────────────────────────────────────────────
    {
        CComPtr<IDiaEnumSymbols> ppEnum;
        if (diaSym->findChildren(SymTagFriend, nullptr, nsNone, &ppEnum) == S_OK) {
            IDiaSymbol* pRaw = nullptr;
            ULONG celt = 0;
            while (ppEnum->Next(1, &pRaw, &celt) == S_OK && celt == 1) {
                CComPtr<IDiaSymbol> pFriend(pRaw);
                CComPtr<IDiaSymbol> pFriendType;
                if (pFriend->get_type(&pFriendType) == S_OK && pFriendType) {
                    DWORD friendSymId = 0;
                    pFriendType->get_symIndexId(&friendSymId);
                    SymbolId friendId = index.findByPdbId(friendSymId);
                    if (friendId == INVALID_SYMBOL_ID) {
                        DWORD friendTag = 0;
                        pFriendType->get_symTag(&friendTag);
                        friendId = index.addSymbol(extractSymbol(pFriendType, diaTagToKind(friendTag)));
                    }
                    friends.push_back(friendId);
                }
            }
        }
    }

    // ── Compiland (lexical parent) ──────────────────────────────────────────
    SymbolId compilandId = resolveLexicalCompiland(diaSym, index);

    // ── 6. Derived classes ────────────────────────────────────────────────────
    // a) Register this UDT as derived on each of its base classes.
    // b) Scan all loaded UDTs — any that list targetId as a base are our derived.
    std::vector<SymbolId> derivedClasses;

    for (SymbolId udtId : index.udts()) {
        if (udtId == targetId) continue;
        auto* udtSym = index.getSymbol(udtId);
        if (!udtSym || !udtSym->childrenLoaded) continue;
        auto* otherUdt = std::get_if<UdtData>(&udtSym->kindData);
        if (!otherUdt) continue;
        for (SymbolId baseId : otherUdt->baseClasses) {
            if (baseId == targetId) {
                derivedClasses.push_back(udtId);
                break;
            }
        }
    }

    // ── Write back (index may have reallocated above, so re-fetch by id) ─────
    auto* node = index.getSymbol(targetId);
    if (node) {
        auto& udt            = std::get<UdtData>(node->kindData);
        udt.members          = std::move(members);
        udt.baseClasses      = std::move(baseClasses);
        udt.derivedClasses   = std::move(derivedClasses);
        udt.friends          = std::move(friends);
        node->children       = std::move(children);
        node->compilandId    = compilandId;
        node->templateArgs   = resolveTemplateArgs(node->name, index);
        node->childrenLoaded = true;
    }

    // Register this UDT as a derived class on each base (if base is already loaded)
    for (SymbolId baseId : std::get<UdtData>(index.getSymbol(targetId)->kindData).baseClasses) {
        auto* baseSym = index.getSymbol(baseId);
        if (!baseSym || !baseSym->childrenLoaded) continue;
        auto* baseUdt = std::get_if<UdtData>(&baseSym->kindData);
        if (!baseUdt) continue;
        // Avoid duplicates
        bool found = false;
        for (SymbolId d : baseUdt->derivedClasses)
            if (d == targetId) { found = true; break; }
        if (!found)
            baseUdt->derivedClasses.push_back(targetId);
    }
}

static int64_t variantToInt64(const VARIANT& v)
{
    switch (v.vt) {
    case VT_I1:   return v.cVal;
    case VT_I2:   return v.iVal;
    case VT_I4:   return v.lVal;
    case VT_I8:   return v.llVal;
    case VT_UI1:  return v.bVal;
    case VT_UI2:  return v.uiVal;
    case VT_UI4:  return v.ulVal;
    case VT_UI8:  return static_cast<int64_t>(v.ullVal);
    case VT_INT:  return v.intVal;
    case VT_UINT: return v.uintVal;
    default:      return 0;
    }
}

void PdbSession::getEnumSymbolData(SymbolId targetId, IDiaSymbol* diaSym, PdbIndex& index)
{
    std::vector<EnumValue> values;
    std::string underlyingTypeName;

    // Underlying integer type (e.g. int, unsigned int)
    CComPtr<IDiaSymbol> pType;
    if (diaSym->get_type(&pType) == S_OK)
        underlyingTypeName = typeNameFromDia(pType);

    // Enumerate constants
    CComPtr<IDiaEnumSymbols> ppEnum;
    if (diaSym->findChildren(SymTagData, nullptr, nsNone, &ppEnum) == S_OK) {
        IDiaSymbol* pRaw = nullptr;
        ULONG celt = 0;
        while (ppEnum->Next(1, &pRaw, &celt) == S_OK && celt == 1) {
            CComPtr<IDiaSymbol> pConst(pRaw);

            DWORD dataKind = 0;
            pConst->get_dataKind(&dataKind);
            if (dataKind != DataIsConstant)
                continue;

            EnumValue ev;
            CComBSTR name;
            if (pConst->get_name(&name) == S_OK)
                ev.name = bstrToUtf8(name);

            VARIANT val;
            VariantInit(&val);
            if (pConst->get_value(&val) == S_OK)
                ev.value = variantToInt64(val);
            VariantClear(&val);

            values.push_back(std::move(ev));
        }
    }

    // ── Compiland (lexical parent) ──────────────────────────────────────────
    SymbolId compilandId = resolveLexicalCompiland(diaSym, index);

    auto* node = index.getSymbol(targetId);
    if (node) {
        std::get<EnumData>(node->kindData).enumValues = std::move(values);
        node->typeName       = std::move(underlyingTypeName);
        node->prettyTypeName = prettifyName(node->typeName);
        node->compilandId    = compilandId;
        node->childrenLoaded = true;
    }
}

void PdbSession::getTypedefSymbolData(SymbolId targetId, IDiaSymbol* diaSym, PdbIndex& index)
{
    std::string resolvedName;
    SymbolId resolvedTypeId = INVALID_SYMBOL_ID;

    CComPtr<IDiaSymbol> pType;
    if (diaSym->get_type(&pType) == S_OK) {
        resolvedName = typeNameFromDia(pType);

        DWORD typeSymId = 0;
        if (pType->get_symIndexId(&typeSymId) == S_OK) {
            resolvedTypeId = index.findByPdbId(typeSymId);
            if (resolvedTypeId == INVALID_SYMBOL_ID) {
                DWORD typeTag = 0;
                pType->get_symTag(&typeTag);
                resolvedTypeId = index.addSymbol(extractSymbol(pType, diaTagToKind(typeTag)));
            }
        }
    }

    // ── Compiland (lexical parent) ──────────────────────────────────────────
    SymbolId compilandId = resolveLexicalCompiland(diaSym, index);

    auto* node = index.getSymbol(targetId);
    if (node) {
        node->typeName       = std::move(resolvedName);
        node->prettyTypeName = prettifyName(node->typeName);
        node->typeId         = resolvedTypeId;
        node->compilandId    = compilandId;
        node->childrenLoaded = true;
    }
}

void PdbSession::getDataSymbolData(SymbolId targetId, IDiaSymbol* diaSym, PdbIndex& index)
{
    std::string resolvedTypeName;
    SymbolId resolvedTypeId = INVALID_SYMBOL_ID;
    uint64_t sizeBytes = 0;

    CComPtr<IDiaSymbol> pType;
    if (diaSym->get_type(&pType) == S_OK && pType) {
        resolvedTypeName = typeNameFromDia(pType);

        ULONGLONG len = 0;
        if (pType->get_length(&len) == S_OK)
            sizeBytes = len;

        resolvedTypeId = addTypeChainToIndex(pType, index);
    }

    // ── Compiland (lexical parent) ──────────────────────────────────────────
    SymbolId compilandId = resolveLexicalCompiland(diaSym, index);

    auto* node = index.getSymbol(targetId);
    if (node) {
        node->typeName       = std::move(resolvedTypeName);
        node->prettyTypeName = prettifyName(node->typeName);
        node->typeId         = resolvedTypeId;
        if (node->sizeBytes == 0)
            node->sizeBytes = sizeBytes;
        node->compilandId    = compilandId;
        node->childrenLoaded = true;
    }
}
