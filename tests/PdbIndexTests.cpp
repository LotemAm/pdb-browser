#include <gtest/gtest.h>

#include "pdb/PdbIndex.h"

// ── Helper to build a minimal SymbolNode ─────────────────────────────────────

static SymbolNode makeSymbol(PdbId pdbId, SymbolKind kind,
                              const std::string& name = {},
                              uint32_t rva = 0)
{
    SymbolNode sym;
    sym.pdbId = pdbId;
    sym.kind  = kind;
    sym.name  = name;
    sym.rva   = rva;
    return sym;
}

// ═════════════════════════════════════════════════════════════════════════════
// addSymbol
// ═════════════════════════════════════════════════════════════════════════════

TEST(PdbIndex, AddSymbolReturnsSequentialIds)
{
    PdbIndex index;
    SymbolId id0 = index.addSymbol(makeSymbol(100, SymbolKind::Function, "foo"));
    SymbolId id1 = index.addSymbol(makeSymbol(101, SymbolKind::Function, "bar"));

    EXPECT_EQ(id0, 0u);
    EXPECT_EQ(id1, 1u);
    EXPECT_EQ(index.symbolCount(), 2u);
}

TEST(PdbIndex, AddSymbolSetsIdOnNode)
{
    PdbIndex index;
    SymbolId id = index.addSymbol(makeSymbol(100, SymbolKind::UDT, "Foo"));
    const SymbolNode* sym = index.getSymbol(id);

    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->id, id);
    EXPECT_EQ(sym->name, "Foo");
}

TEST(PdbIndex, DeduplicatesByPdbId)
{
    PdbIndex index;
    SymbolId id0 = index.addSymbol(makeSymbol(42, SymbolKind::Function, "foo"));
    SymbolId id1 = index.addSymbol(makeSymbol(42, SymbolKind::Function, "foo_dup"));

    EXPECT_EQ(id0, id1);
    EXPECT_EQ(index.symbolCount(), 1u);
}

TEST(PdbIndex, ZeroPdbIdIsNotDeduplicated)
{
    PdbIndex index;
    SymbolId id0 = index.addSymbol(makeSymbol(0, SymbolKind::Function, "a"));
    SymbolId id1 = index.addSymbol(makeSymbol(0, SymbolKind::Function, "b"));

    EXPECT_NE(id0, id1);
    EXPECT_EQ(index.symbolCount(), 2u);
}

// ═════════════════════════════════════════════════════════════════════════════
// Kind categorization
// ═════════════════════════════════════════════════════════════════════════════

TEST(PdbIndex, CategorizesCompilands)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(1, SymbolKind::Compiland, "main.obj"));

    EXPECT_EQ(index.compilands().size(), 1u);
    EXPECT_TRUE(index.udts().empty());
}

TEST(PdbIndex, CategorizesUdts)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(1, SymbolKind::UDT, "MyClass"));

    EXPECT_EQ(index.udts().size(), 1u);
    EXPECT_TRUE(index.functions().empty());
}

TEST(PdbIndex, CategorizesEnums)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(1, SymbolKind::Enum, "Color"));

    EXPECT_EQ(index.enums().size(), 1u);
}

TEST(PdbIndex, CategorizesTypedefs)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(1, SymbolKind::Typedef, "DWORD"));

    EXPECT_EQ(index.typedefs().size(), 1u);
}

TEST(PdbIndex, CategorizesFunctions)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(1, SymbolKind::Function, "main"));

    EXPECT_EQ(index.functions().size(), 1u);
}

TEST(PdbIndex, CategorizesDataAsGlobal)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(1, SymbolKind::Data, "g_value"));

    EXPECT_EQ(index.globals().size(), 1u);
}

TEST(PdbIndex, CategorizesPublicSymbolAsGlobal)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(1, SymbolKind::PublicSymbol, "?foo@@YAHXZ"));

    EXPECT_EQ(index.globals().size(), 1u);
}

TEST(PdbIndex, UnknownKindNotCategorized)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(1, SymbolKind::Unknown, "???"));

    EXPECT_TRUE(index.compilands().empty());
    EXPECT_TRUE(index.udts().empty());
    EXPECT_TRUE(index.enums().empty());
    EXPECT_TRUE(index.typedefs().empty());
    EXPECT_TRUE(index.functions().empty());
    EXPECT_TRUE(index.globals().empty());
    EXPECT_EQ(index.symbolCount(), 1u);
}

// ═════════════════════════════════════════════════════════════════════════════
// findByPdbId
// ═════════════════════════════════════════════════════════════════════════════

TEST(PdbIndex, FindByPdbIdReturnsCorrectSymbol)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(10, SymbolKind::Function, "a"));
    index.addSymbol(makeSymbol(20, SymbolKind::Function, "b"));
    index.addSymbol(makeSymbol(30, SymbolKind::Function, "c"));

    SymbolId id = index.findByPdbId(20);
    ASSERT_NE(id, INVALID_SYMBOL_ID);

    const auto* sym = index.getSymbol(id);
    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->name, "b");
}

TEST(PdbIndex, FindByPdbIdReturnsInvalidForMissing)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(10, SymbolKind::Function, "a"));

    EXPECT_EQ(index.findByPdbId(999), INVALID_SYMBOL_ID);
}

// ═════════════════════════════════════════════════════════════════════════════
// findByName
// ═════════════════════════════════════════════════════════════════════════════

TEST(PdbIndex, FindByNameReturnsSingleMatch)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(1, SymbolKind::Function, "foo"));
    index.addSymbol(makeSymbol(2, SymbolKind::Function, "bar"));

    auto results = index.findByName("foo");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(index.getSymbol(results[0])->name, "foo");
}

TEST(PdbIndex, FindByNameReturnsMultipleOverloads)
{
    PdbIndex index;
    // pdbId 0 → not deduplicated
    index.addSymbol(makeSymbol(0, SymbolKind::Function, "foo"));
    index.addSymbol(makeSymbol(0, SymbolKind::Function, "foo"));

    auto results = index.findByName("foo");
    EXPECT_EQ(results.size(), 2u);
}

TEST(PdbIndex, FindByNameReturnsEmptyForMissing)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(1, SymbolKind::Function, "foo"));

    EXPECT_TRUE(index.findByName("bar").empty());
}

TEST(PdbIndex, EmptyNameNotIndexed)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(1, SymbolKind::Unknown));

    EXPECT_TRUE(index.findByName("").empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// findByRva
// ═════════════════════════════════════════════════════════════════════════════

TEST(PdbIndex, FindByRvaReturnsCorrectSymbol)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(1, SymbolKind::Function, "a", 0x1000));
    index.addSymbol(makeSymbol(2, SymbolKind::Function, "b", 0x2000));

    SymbolId id = index.findByRva(0x2000);
    ASSERT_NE(id, INVALID_SYMBOL_ID);
    EXPECT_EQ(index.getSymbol(id)->name, "b");
}

TEST(PdbIndex, FindByRvaReturnsInvalidForMissing)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(1, SymbolKind::Function, "a", 0x1000));

    EXPECT_EQ(index.findByRva(0xDEAD), INVALID_SYMBOL_ID);
}

TEST(PdbIndex, ZeroRvaNotIndexed)
{
    PdbIndex index;
    index.addSymbol(makeSymbol(1, SymbolKind::Function, "a", 0));

    EXPECT_EQ(index.findByRva(0), INVALID_SYMBOL_ID);
}

// ═════════════════════════════════════════════════════════════════════════════
// getSymbol
// ═════════════════════════════════════════════════════════════════════════════

TEST(PdbIndex, GetSymbolReturnsNullForOutOfRange)
{
    PdbIndex index;
    EXPECT_EQ(index.getSymbol(0), nullptr);
    EXPECT_EQ(index.getSymbol(INVALID_SYMBOL_ID), nullptr);
}

TEST(PdbIndex, GetSymbolReturnsNullForEmptyIndex)
{
    PdbIndex index;
    EXPECT_EQ(index.getSymbol(0), nullptr);
}

// ═════════════════════════════════════════════════════════════════════════════
// pdbPath
// ═════════════════════════════════════════════════════════════════════════════

TEST(PdbIndex, PdbPathDefaultEmpty)
{
    PdbIndex index;
    EXPECT_TRUE(index.pdbPath().empty());
}

TEST(PdbIndex, SetAndGetPdbPath)
{
    PdbIndex index;
    index.setPdbPath("C:\\symbols\\test.pdb");
    EXPECT_EQ(index.pdbPath(), "C:\\symbols\\test.pdb");
}
