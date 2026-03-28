#include "Exporter.h"
#include "pdb/PdbIndex.h"

#include <nlohmann/json.hpp>

#include <format>
#include <fstream>

using json = nlohmann::json;

static json templateArgToJson(const TemplateArg& arg)
{
    json j;
    j["text"] = arg.text;
    if (!arg.prettyText.empty() && arg.prettyText != arg.text)
        j["pretty_text"] = arg.prettyText;
    if (arg.typeId != INVALID_SYMBOL_ID)
        j["type_id"] = arg.typeId;
    return j;
}

static json symbolToJson(const PdbIndex& index, const SymbolNode& sym,
                          const ExportOptions& opts)
{
    json j;
    j["id"]   = sym.id;
    j["kind"] = kindStr(sym.kind);
    j["name"] = sym.name;
    if (!sym.undecoratedName.empty() && sym.undecoratedName != sym.name)
        j["undecorated_name"] = sym.undecoratedName;
    if (sym.rva)        j["rva"]  = sym.rva;
    if (sym.sizeBytes)  j["size"] = sym.sizeBytes;
    if (!sym.sourceFile.empty()) {
        j["source_file"] = sym.sourceFile;
        j["source_line"] = sym.sourceLine;
    }
    if (!sym.typeName.empty())
        j["type_name"] = sym.typeName;
    if (sym.typeId != INVALID_SYMBOL_ID)
        j["type_id"] = sym.typeId;
    if (sym.parentId != INVALID_SYMBOL_ID)
        j["parent_id"] = sym.parentId;

    // Template arguments
    if (opts.includeTemplateArgs && !sym.templateArgs.empty()) {
        json args = json::array();
        for (const auto& ta : sym.templateArgs)
            args.push_back(templateArgToJson(ta));
        j["template_args"] = args;
    }

    // ── Function ─────────────────────────────────────────────────────────────
    if (const auto* fn = std::get_if<FunctionData>(&sym.kindData)) {
        j["calling_convention"] = callingConvStr(fn->callingConvention);
        j["inline"]    = fn->isInline;
        j["virtual"]   = fn->isVirtual;
        j["pure"]      = fn->isPure;
        j["static"]    = fn->isStatic;
        j["no_return"] = fn->isNoReturn;
    }

    // ── UDT ──────────────────────────────────────────────────────────────────
    if (const auto* udt = std::get_if<UdtData>(&sym.kindData)) {
        j["udt_tag"]     = udtTagStr(udt->udtTag);
        j["forward_ref"] = udt->isForwardRef;

        if (opts.includeBaseClasses && !udt->baseClasses.empty()) {
            json bases = json::array();
            for (SymbolId baseId : udt->baseClasses) {
                json bj;
                bj["id"] = baseId;
                if (const SymbolNode* base = index.getSymbol(baseId))
                    bj["name"] = base->name;
                bases.push_back(bj);
            }
            j["base_classes"] = bases;
        }

        if (opts.includeFriends && !udt->friends.empty()) {
            json flist = json::array();
            for (SymbolId fid : udt->friends) {
                json fj;
                fj["id"] = fid;
                if (const SymbolNode* fr = index.getSymbol(fid))
                    fj["name"] = fr->name;
                flist.push_back(fj);
            }
            j["friends"] = flist;
        }

        if (opts.includeMembers && !udt->members.empty()) {
            json members = json::array();
            for (const auto& m : udt->members) {
                json mj;
                mj["name"]   = m.name;
                mj["type"]   = m.typeName;
                mj["offset"] = m.offset;
                mj["size"]   = m.sizeBytes;
                if (m.typeId != INVALID_SYMBOL_ID)
                    mj["type_id"] = m.typeId;
                if (m.bitSize > 0) {
                    mj["bit_offset"] = m.bitOffset;
                    mj["bit_size"]   = m.bitSize;
                }
                members.push_back(mj);
            }
            j["members"] = members;
        }
    }

    // ── Enum ─────────────────────────────────────────────────────────────────
    if (const auto* en = std::get_if<EnumData>(&sym.kindData)) {
        if (opts.includeEnumValues && !en->enumValues.empty()) {
            json vals = json::array();
            for (const auto& ev : en->enumValues) {
                json vj;
                vj["name"]  = ev.name;
                vj["value"] = ev.value;
                vals.push_back(vj);
            }
            j["values"] = vals;
        }
    }

    // ── Data symbol ──────────────────────────────────────────────────────────
    if (const auto* ds = std::get_if<DataSymData>(&sym.kindData)) {
        j["data_kind"] = ds->dataKind;
        if (ds->hasConstValue)
            j["const_value"] = ds->constValue;
    }

    // ── Compiland reference (all symbol kinds) ──────────────────────────────
    if (sym.compilandId != INVALID_SYMBOL_ID) {
        if (const auto* comp = index.getSymbol(sym.compilandId))
            j["compiland"] = comp->name;
    }

    // ── Compiland own data ──────────────────────────────────────────────────
    if (const auto* comp = std::get_if<CompilandData>(&sym.kindData)) {
        if (opts.includeSourceFiles && !comp->sourceFiles.empty()) {
            json files = json::array();
            for (const auto& sf : comp->sourceFiles)
                files.push_back(sf);
            j["source_files"] = files;
        }
    }

    return j;
}

// ── CSV helpers ──────────────────────────────────────────────────────────────

// RFC 4180 CSV escaping: wrap in double-quotes, doubling any internal quotes.
static std::string csvEscape(const std::string& s)
{
    if (s.empty()) return "\"\"";
    bool needQuote = false;
    for (char c : s) {
        if (c == '"' || c == ',' || c == '\n' || c == '\r') {
            needQuote = true;
            break;
        }
    }
    if (!needQuote) return s;

    std::string out;
    out.reserve(s.size() + 4);
    out += '"';
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

// ── Exporter ─────────────────────────────────────────────────────────────────

std::expected<void, std::string>
Exporter::toJson(const PdbIndex& index,
                 const std::vector<SymbolId>& ids,
                 const std::string& outputPath,
                 const ExportOptions& options)
{
    json root = json::object();
    root["pdb_path"]     = index.pdbPath();
    root["symbol_count"] = ids.size();

    json symbols = json::array();
    for (SymbolId id : ids) {
        const SymbolNode* sym = index.getSymbol(id);
        if (sym) symbols.push_back(symbolToJson(index, *sym, options));
    }
    root["symbols"] = symbols;

    std::ofstream f(outputPath);
    if (!f)
        return std::unexpected(std::format("Cannot open file for writing: {}", outputPath));

    f << root.dump(2);
    if (!f.good())
        return std::unexpected(std::format("Write error: {}", outputPath));
    return {};
}

std::expected<void, std::string>
Exporter::toCsv(const PdbIndex& index,
                const std::vector<SymbolId>& ids,
                const std::string& outputPath,
                const ExportOptions& /*options*/)
{
    std::ofstream f(outputPath);
    if (!f)
        return std::unexpected(std::format("Cannot open file for writing: {}", outputPath));

    // Header
    f << "id,kind,name,undecorated_name,type_name,rva,size,source_file,source_line,parent_id\n";

    for (SymbolId id : ids) {
        const SymbolNode* sym = index.getSymbol(id);
        if (!sym) continue;

        std::string parentStr;
        if (sym->parentId != INVALID_SYMBOL_ID)
            parentStr = std::format("{}", sym->parentId);

        f << std::format("{},{},{},{},{},{},{},{},{},{}\n",
            sym->id,
            kindStr(sym->kind),
            csvEscape(sym->name),
            csvEscape(sym->undecoratedName),
            csvEscape(sym->typeName),
            sym->rva,
            sym->sizeBytes,
            csvEscape(sym->sourceFile),
            sym->sourceLine,
            parentStr);
    }

    if (!f.good())
        return std::unexpected(std::format("Write error: {}", outputPath));
    return {};
}
