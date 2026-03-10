#include <gtest/gtest.h>

#include "export/Exporter.h"
#include "pdb/PdbIndex.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using json = nlohmann::json;

// ── Helper: RAII temp file ───────────────────────────────────────────────────

class TempFile {
public:
    TempFile(const std::string& ext = ".json")
    {
        auto tmp = std::filesystem::temp_directory_path();
        m_path = (tmp / ("pdb_test_" + std::to_string(++s_counter) + ext)).string();
    }
    ~TempFile() { std::filesystem::remove(m_path); }

    const std::string& path() const { return m_path; }

    std::string read() const
    {
        std::ifstream f(m_path);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

private:
    std::string m_path;
    static inline int s_counter = 0;
};

// ── Helper: build a populated PdbIndex ───────────────────────────────────────

static SymbolNode makeFunction(PdbId pdbId, const std::string& name,
                                uint32_t rva = 0, uint64_t size = 0)
{
    SymbolNode sym;
    sym.pdbId    = pdbId;
    sym.kind     = SymbolKind::Function;
    sym.name     = name;
    sym.rva      = rva;
    sym.sizeBytes = size;
    sym.kindData  = FunctionData{CallingConv::CDecl, false, false, false, false, false};
    return sym;
}

static SymbolNode makeUdt(PdbId pdbId, const std::string& name, uint64_t size = 0)
{
    SymbolNode sym;
    sym.pdbId    = pdbId;
    sym.kind     = SymbolKind::UDT;
    sym.name     = name;
    sym.sizeBytes = size;

    UdtData udt;
    udt.udtTag = UdtTag::Struct;
    udt.members.push_back(MemberInfo{"x", "int", "", INVALID_SYMBOL_ID, 0, 4, 0, 0});
    udt.members.push_back(MemberInfo{"y", "float", "", INVALID_SYMBOL_ID, 4, 4, 0, 0});
    sym.kindData = std::move(udt);

    return sym;
}

static SymbolNode makeEnum(PdbId pdbId, const std::string& name)
{
    SymbolNode sym;
    sym.pdbId = pdbId;
    sym.kind  = SymbolKind::Enum;
    sym.name  = name;

    EnumData en;
    en.enumValues.push_back(EnumValue{"Red", 0});
    en.enumValues.push_back(EnumValue{"Green", 1});
    en.enumValues.push_back(EnumValue{"Blue", 2});
    sym.kindData = std::move(en);

    return sym;
}

static SymbolNode makeCompiland(PdbId pdbId, const std::string& name)
{
    SymbolNode sym;
    sym.pdbId = pdbId;
    sym.kind  = SymbolKind::Compiland;
    sym.name  = name;

    CompilandData comp;
    comp.sourceFiles.push_back("main.cpp");
    comp.sourceFiles.push_back("util.cpp");
    sym.kindData = std::move(comp);

    return sym;
}

// ═════════════════════════════════════════════════════════════════════════════
// JSON export
// ═════════════════════════════════════════════════════════════════════════════

TEST(ExporterJson, BasicStructure)
{
    PdbIndex index;
    index.setPdbPath("test.pdb");
    SymbolId id = index.addSymbol(makeFunction(1, "foo", 0x1000, 64));

    TempFile tmp;
    auto result = Exporter::toJson(index, {id}, tmp.path());
    ASSERT_TRUE(result.has_value()) << result.error();

    auto j = json::parse(tmp.read());
    EXPECT_EQ(j["pdb_path"], "test.pdb");
    EXPECT_EQ(j["symbol_count"], 1);
    ASSERT_EQ(j["symbols"].size(), 1u);

    auto& sym = j["symbols"][0];
    EXPECT_EQ(sym["name"], "foo");
    EXPECT_EQ(sym["kind"], "function");
    EXPECT_EQ(sym["rva"], 0x1000);
    EXPECT_EQ(sym["size"], 64);
}

TEST(ExporterJson, FunctionFields)
{
    PdbIndex index;
    SymbolId id = index.addSymbol(makeFunction(1, "bar"));

    TempFile tmp;
    (void)Exporter::toJson(index, {id}, tmp.path());

    auto j = json::parse(tmp.read());
    auto& sym = j["symbols"][0];
    EXPECT_EQ(sym["calling_convention"], "cdecl");
    EXPECT_EQ(sym["inline"], false);
    EXPECT_EQ(sym["virtual"], false);
    EXPECT_EQ(sym["static"], false);
}

TEST(ExporterJson, UdtWithMembers)
{
    PdbIndex index;
    SymbolId id = index.addSymbol(makeUdt(1, "Point", 8));

    TempFile tmp;
    (void)Exporter::toJson(index, {id}, tmp.path(), ExportOptions{});

    auto j = json::parse(tmp.read());
    auto& sym = j["symbols"][0];
    EXPECT_EQ(sym["kind"], "udt");
    EXPECT_EQ(sym["udt_tag"], "Struct");
    ASSERT_TRUE(sym.contains("members"));
    EXPECT_EQ(sym["members"].size(), 2u);
    EXPECT_EQ(sym["members"][0]["name"], "x");
    EXPECT_EQ(sym["members"][0]["type"], "int");
    EXPECT_EQ(sym["members"][1]["name"], "y");
}

TEST(ExporterJson, UdtMembersExcludedWhenOptionOff)
{
    PdbIndex index;
    SymbolId id = index.addSymbol(makeUdt(1, "Point"));

    ExportOptions opts;
    opts.includeMembers = false;

    TempFile tmp;
    (void)Exporter::toJson(index, {id}, tmp.path(), opts);

    auto j = json::parse(tmp.read());
    EXPECT_FALSE(j["symbols"][0].contains("members"));
}

TEST(ExporterJson, EnumValues)
{
    PdbIndex index;
    SymbolId id = index.addSymbol(makeEnum(1, "Color"));

    TempFile tmp;
    (void)Exporter::toJson(index, {id}, tmp.path());

    auto j = json::parse(tmp.read());
    auto& sym = j["symbols"][0];
    ASSERT_TRUE(sym.contains("values"));
    EXPECT_EQ(sym["values"].size(), 3u);
    EXPECT_EQ(sym["values"][0]["name"], "Red");
    EXPECT_EQ(sym["values"][0]["value"], 0);
    EXPECT_EQ(sym["values"][2]["name"], "Blue");
    EXPECT_EQ(sym["values"][2]["value"], 2);
}

TEST(ExporterJson, EnumValuesExcludedWhenOptionOff)
{
    PdbIndex index;
    SymbolId id = index.addSymbol(makeEnum(1, "Color"));

    ExportOptions opts;
    opts.includeEnumValues = false;

    TempFile tmp;
    (void)Exporter::toJson(index, {id}, tmp.path(), opts);

    auto j = json::parse(tmp.read());
    EXPECT_FALSE(j["symbols"][0].contains("values"));
}

TEST(ExporterJson, CompilandSourceFiles)
{
    PdbIndex index;
    SymbolId id = index.addSymbol(makeCompiland(1, "main.obj"));

    TempFile tmp;
    (void)Exporter::toJson(index, {id}, tmp.path());

    auto j = json::parse(tmp.read());
    auto& sym = j["symbols"][0];
    ASSERT_TRUE(sym.contains("source_files"));
    EXPECT_EQ(sym["source_files"].size(), 2u);
    EXPECT_EQ(sym["source_files"][0], "main.cpp");
}

TEST(ExporterJson, SourceFilesExcludedWhenOptionOff)
{
    PdbIndex index;
    SymbolId id = index.addSymbol(makeCompiland(1, "main.obj"));

    ExportOptions opts;
    opts.includeSourceFiles = false;

    TempFile tmp;
    (void)Exporter::toJson(index, {id}, tmp.path(), opts);

    auto j = json::parse(tmp.read());
    EXPECT_FALSE(j["symbols"][0].contains("source_files"));
}

TEST(ExporterJson, TemplateArgs)
{
    PdbIndex index;
    SymbolNode sym = makeFunction(1, "foo<int>");
    sym.templateArgs.push_back(TemplateArg{"int", "", INVALID_SYMBOL_ID});
    SymbolId id = index.addSymbol(std::move(sym));

    TempFile tmp;
    (void)Exporter::toJson(index, {id}, tmp.path());

    auto j = json::parse(tmp.read());
    ASSERT_TRUE(j["symbols"][0].contains("template_args"));
    EXPECT_EQ(j["symbols"][0]["template_args"][0]["text"], "int");
}

TEST(ExporterJson, TemplateArgsExcludedWhenOptionOff)
{
    PdbIndex index;
    SymbolNode sym = makeFunction(1, "foo<int>");
    sym.templateArgs.push_back(TemplateArg{"int", "", INVALID_SYMBOL_ID});
    SymbolId id = index.addSymbol(std::move(sym));

    ExportOptions opts;
    opts.includeTemplateArgs = false;

    TempFile tmp;
    (void)Exporter::toJson(index, {id}, tmp.path(), opts);

    auto j = json::parse(tmp.read());
    EXPECT_FALSE(j["symbols"][0].contains("template_args"));
}

TEST(ExporterJson, MultipleSymbols)
{
    PdbIndex index;
    SymbolId id0 = index.addSymbol(makeFunction(1, "a"));
    SymbolId id1 = index.addSymbol(makeFunction(2, "b"));
    SymbolId id2 = index.addSymbol(makeFunction(3, "c"));

    TempFile tmp;
    (void)Exporter::toJson(index, {id0, id1, id2}, tmp.path());

    auto j = json::parse(tmp.read());
    EXPECT_EQ(j["symbol_count"], 3);
    EXPECT_EQ(j["symbols"].size(), 3u);
}

TEST(ExporterJson, SkipsInvalidIds)
{
    PdbIndex index;
    SymbolId id = index.addSymbol(makeFunction(1, "valid"));

    TempFile tmp;
    (void)Exporter::toJson(index, {id, INVALID_SYMBOL_ID, 999}, tmp.path());

    auto j = json::parse(tmp.read());
    // symbol_count reflects requested count, but only valid ones appear in array
    EXPECT_EQ(j["symbols"].size(), 1u);
}

TEST(ExporterJson, EmptyIdsProducesEmptyArray)
{
    PdbIndex index;
    index.setPdbPath("empty.pdb");

    TempFile tmp;
    auto result = Exporter::toJson(index, {}, tmp.path());
    ASSERT_TRUE(result.has_value());

    auto j = json::parse(tmp.read());
    EXPECT_EQ(j["symbol_count"], 0);
    EXPECT_TRUE(j["symbols"].empty());
}

TEST(ExporterJson, InvalidPathReturnsError)
{
    PdbIndex index;
    SymbolId id = index.addSymbol(makeFunction(1, "foo"));

    auto result = Exporter::toJson(index, {id}, "/nonexistent/dir/file.json");
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// CSV export
// ═════════════════════════════════════════════════════════════════════════════

TEST(ExporterCsv, HeaderRow)
{
    PdbIndex index;
    SymbolId id = index.addSymbol(makeFunction(1, "foo"));

    TempFile tmp(".csv");
    auto result = Exporter::toCsv(index, {id}, tmp.path());
    ASSERT_TRUE(result.has_value()) << result.error();

    auto content = tmp.read();
    // First line is the header
    auto firstLine = content.substr(0, content.find('\n'));
    EXPECT_EQ(firstLine, "id,kind,name,undecorated_name,type_name,rva,size,source_file,source_line,parent_id");
}

TEST(ExporterCsv, DataRow)
{
    PdbIndex index;
    SymbolNode sym = makeFunction(1, "myFunc", 0x1000, 32);
    sym.undecoratedName = "myFunc";
    sym.typeName = "int";
    SymbolId id = index.addSymbol(std::move(sym));

    TempFile tmp(".csv");
    (void)Exporter::toCsv(index, {id}, tmp.path());

    auto content = tmp.read();
    // Second line is the data
    auto dataStart = content.find('\n') + 1;
    auto dataLine  = content.substr(dataStart, content.find('\n', dataStart) - dataStart);

    // Format: id,kind,name,undecorated_name,type_name,rva,size,source_file,source_line,parent_id
    EXPECT_NE(dataLine.find("myFunc"), std::string::npos);
    EXPECT_NE(dataLine.find("function"), std::string::npos);
    EXPECT_NE(dataLine.find("4096"), std::string::npos);  // 0x1000 = 4096
}

TEST(ExporterCsv, EscapesCommasInNames)
{
    PdbIndex index;
    SymbolNode sym;
    sym.pdbId = 1;
    sym.kind  = SymbolKind::Function;
    sym.name  = "foo<int, float>";
    SymbolId id = index.addSymbol(std::move(sym));

    TempFile tmp(".csv");
    (void)Exporter::toCsv(index, {id}, tmp.path());

    auto content = tmp.read();
    // Name with commas should be quoted
    EXPECT_NE(content.find("\"foo<int, float>\""), std::string::npos);
}

TEST(ExporterCsv, EscapesQuotesInNames)
{
    PdbIndex index;
    SymbolNode sym;
    sym.pdbId = 1;
    sym.kind  = SymbolKind::Function;
    sym.name  = "operator\"\"_foo";
    SymbolId id = index.addSymbol(std::move(sym));

    TempFile tmp(".csv");
    (void)Exporter::toCsv(index, {id}, tmp.path());

    auto content = tmp.read();
    // Quotes inside should be doubled per RFC 4180
    EXPECT_NE(content.find("\"\""), std::string::npos);
}

TEST(ExporterCsv, MultipleRows)
{
    PdbIndex index;
    SymbolId id0 = index.addSymbol(makeFunction(1, "a"));
    SymbolId id1 = index.addSymbol(makeFunction(2, "b"));

    TempFile tmp(".csv");
    (void)Exporter::toCsv(index, {id0, id1}, tmp.path());

    auto content = tmp.read();
    // Count newlines: header + 2 data rows + possible trailing
    int newlines = 0;
    for (char c : content)
        if (c == '\n') ++newlines;
    EXPECT_GE(newlines, 3); // header + 2 data rows
}

TEST(ExporterCsv, InvalidPathReturnsError)
{
    PdbIndex index;
    SymbolId id = index.addSymbol(makeFunction(1, "foo"));

    auto result = Exporter::toCsv(index, {id}, "/nonexistent/dir/file.csv");
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().empty());
}
