#include <gtest/gtest.h>

#include "codegen/CodeGen.h"
#include "codegen/CodeGenC.h"
#include "codegen/CodeGenCpp.h"
#include "codegen/CodeGenRust.h"
#include "pdb/PdbIndex.h"

// ── Helpers ──────────────────────────────────────────────────────────────────

static SymbolNode makeUdt(const std::string& name, UdtTag tag = UdtTag::Struct)
{
    SymbolNode sym;
    sym.pdbId = 0;
    sym.kind  = SymbolKind::UDT;
    sym.name  = name;
    sym.undecoratedName = name;
    UdtData udt;
    udt.udtTag = tag;
    sym.kindData = std::move(udt);
    return sym;
}

static MemberInfo makeMember(const std::string& name, const std::string& type,
                             uint32_t offset, uint32_t size,
                             uint8_t bitOffset = 0, uint8_t bitSize = 0)
{
    MemberInfo m;
    m.name       = name;
    m.typeName   = type;
    m.offset     = offset;
    m.sizeBytes  = size;
    m.bitOffset  = bitOffset;
    m.bitSize    = bitSize;
    return m;
}

static SymbolNode makeEnum(const std::string& name, const std::string& underlying = {})
{
    SymbolNode sym;
    sym.pdbId = 0;
    sym.kind  = SymbolKind::Enum;
    sym.name  = name;
    sym.undecoratedName = name;
    sym.typeName = underlying;
    sym.kindData = EnumData{};
    return sym;
}

static SymbolNode makeFunction(const std::string& name, const std::string& undec = {})
{
    SymbolNode sym;
    sym.pdbId = 0;
    sym.kind  = SymbolKind::Function;
    sym.name  = name;
    sym.undecoratedName = undec.empty() ? name : undec;
    sym.kindData = FunctionData{};
    return sym;
}

static SymbolNode makeData(const std::string& name, const std::string& type,
                           int64_t value, bool hasConst = true)
{
    SymbolNode sym;
    sym.pdbId = 0;
    sym.kind  = SymbolKind::Data;
    sym.name  = name;
    sym.undecoratedName = name;
    sym.typeName = type;
    DataSymData dat;
    dat.hasConstValue = hasConst;
    dat.constValue    = value;
    sym.kindData = std::move(dat);
    return sym;
}

// ═════════════════════════════════════════════════════════════════════════════
// applicableLanguages
// ═════════════════════════════════════════════════════════════════════════════

TEST(CodeGen, ApplicableLanguagesUdt)
{
    auto sym = makeUdt("Foo");
    auto langs = applicableLanguages(sym);
    ASSERT_EQ(langs.size(), 3u);
    EXPECT_EQ(langs[0], CodeLang::C);
    EXPECT_EQ(langs[1], CodeLang::Cpp);
    EXPECT_EQ(langs[2], CodeLang::Rust);
}

TEST(CodeGen, ApplicableLanguagesEnum)
{
    auto sym = makeEnum("Colors");
    auto langs = applicableLanguages(sym);
    EXPECT_EQ(langs.size(), 3u);
}

TEST(CodeGen, ApplicableLanguagesDataNoConst)
{
    auto sym = makeData("g_var", "int", 0, /*hasConst=*/false);
    auto langs = applicableLanguages(sym);
    EXPECT_TRUE(langs.empty());
}

TEST(CodeGen, ApplicableLanguagesDataConst)
{
    auto sym = makeData("MAGIC", "int", 42);
    auto langs = applicableLanguages(sym);
    EXPECT_EQ(langs.size(), 3u);
}

TEST(CodeGen, ApplicableLanguagesUnknownKind)
{
    SymbolNode sym;
    sym.kind = SymbolKind::Unknown;
    EXPECT_TRUE(applicableLanguages(sym).empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// C++ code generation
// ═════════════════════════════════════════════════════════════════════════════

TEST(CodeGenCpp, SimpleStruct)
{
    PdbIndex index;
    auto sym = makeUdt("Point");
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("x", "float", 0, 4));
    udt.members.push_back(makeMember("y", "float", 4, 4));
    index.addSymbol(std::move(sym));

    auto code = generateCpp(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("struct Point"), std::string::npos);
    EXPECT_NE(code.find("float x;"), std::string::npos);
    EXPECT_NE(code.find("float y;"), std::string::npos);
    EXPECT_NE(code.find("};"), std::string::npos);
}

TEST(CodeGenCpp, UnionKeyword)
{
    PdbIndex index;
    auto sym = makeUdt("MyUnion", UdtTag::Union);
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("i", "int", 0, 4));
    index.addSymbol(std::move(sym));

    auto code = generateCpp(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("union MyUnion"), std::string::npos);
}

TEST(CodeGenCpp, ClassKeyword)
{
    PdbIndex index;
    auto sym = makeUdt("Widget", UdtTag::Class);
    index.addSymbol(std::move(sym));

    auto code = generateCpp(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("class Widget"), std::string::npos);
}

TEST(CodeGenCpp, Bitfield)
{
    PdbIndex index;
    auto sym = makeUdt("Flags");
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("active", "unsigned int", 0, 4, 0, 1));
    index.addSymbol(std::move(sym));

    auto code = generateCpp(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("unsigned int active : 1;"), std::string::npos);
    EXPECT_NE(code.find("[0:1]"), std::string::npos);
}

TEST(CodeGenCpp, OffsetComment)
{
    PdbIndex index;
    auto sym = makeUdt("Obj");
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("data", "char", 0x10, 1));
    index.addSymbol(std::move(sym));

    auto code = generateCpp(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("+0x10"), std::string::npos);
    EXPECT_NE(code.find("(1 bytes)"), std::string::npos);
}

TEST(CodeGenCpp, ArrayMember)
{
    PdbIndex index;
    auto sym = makeUdt("Mesh");
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("verts", "float[3]", 0, 12));
    index.addSymbol(std::move(sym));

    auto code = generateCpp(index, *index.getSymbol(0), false);
    // Should be "float verts[3]" not "float[3] verts"
    EXPECT_NE(code.find("float verts[3]"), std::string::npos);
}

TEST(CodeGenCpp, BaseClasses)
{
    PdbIndex index;
    auto base = makeUdt("Base");
    SymbolId baseId = index.addSymbol(std::move(base));

    auto derived = makeUdt("Derived");
    auto& udt = std::get<UdtData>(derived.kindData);
    udt.baseClasses.push_back(baseId);
    index.addSymbol(std::move(derived));

    auto code = generateCpp(index, *index.getSymbol(1), false);
    EXPECT_NE(code.find("struct Derived : public Base"), std::string::npos);
}

TEST(CodeGenCpp, EnumClass)
{
    PdbIndex index;
    auto sym = makeEnum("Color", "int");
    auto& en = std::get<EnumData>(sym.kindData);
    en.enumValues.push_back({"Red", 0});
    en.enumValues.push_back({"Green", 1});
    en.enumValues.push_back({"Blue", 2});
    index.addSymbol(std::move(sym));

    auto code = generateCpp(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("enum class Color : int"), std::string::npos);
    EXPECT_NE(code.find("Red = 0"), std::string::npos);
    EXPECT_NE(code.find("Green = 1"), std::string::npos);
    EXPECT_NE(code.find("Blue = 2"), std::string::npos);
    EXPECT_NE(code.find("};"), std::string::npos);
}

TEST(CodeGenCpp, FunctionStripsAccessSpecifier)
{
    PdbIndex index;
    auto sym = makeFunction("Foo::bar", "public: void __cdecl Foo::bar(int)");
    index.addSymbol(std::move(sym));

    auto code = generateCpp(index, *index.getSymbol(0), false);
    EXPECT_EQ(code.find("public:"), std::string::npos);
    EXPECT_NE(code.find("void"), std::string::npos);
}

TEST(CodeGenCpp, ConstexprData)
{
    PdbIndex index;
    auto sym = makeData("MAX_SIZE", "int", 256);
    index.addSymbol(std::move(sym));

    auto code = generateCpp(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("constexpr int MAX_SIZE = 256;"), std::string::npos);
}

TEST(CodeGenCpp, StaticMembers)
{
    PdbIndex index;
    auto parent = makeUdt("Singleton");
    SymbolId parentId = index.addSymbol(std::move(parent));

    auto child = makeData("s_instance", "Singleton *", 0, /*hasConst=*/false);
    child.pdbId = 1;
    child.kind = SymbolKind::Data;
    SymbolId childId = index.addSymbol(std::move(child));

    // Wire up parent-child
    auto* parentSym = const_cast<SymbolNode*>(index.getSymbol(parentId));
    parentSym->children.push_back(childId);

    CodeGenOptions opts;
    opts.includeStatics = true;
    auto code = generateCpp(index, *index.getSymbol(parentId), false, opts);
    EXPECT_NE(code.find("static"), std::string::npos);
    EXPECT_NE(code.find("s_instance"), std::string::npos);
}

TEST(CodeGenCpp, MemberFunctionsGroupedByAccess)
{
    PdbIndex index;
    auto parent = makeUdt("Widget");
    SymbolId parentId = index.addSymbol(std::move(parent));

    auto pubFn = makeFunction("Widget::show", "public: void __cdecl Widget::show(void)");
    pubFn.pdbId = 1;
    SymbolId pubId = index.addSymbol(std::move(pubFn));

    auto privFn = makeFunction("Widget::init", "private: void __cdecl Widget::init(void)");
    privFn.pdbId = 2;
    SymbolId privId = index.addSymbol(std::move(privFn));

    auto* parentSym = const_cast<SymbolNode*>(index.getSymbol(parentId));
    parentSym->children.push_back(pubId);
    parentSym->children.push_back(privId);

    CodeGenOptions opts;
    opts.includeFunctions = true;
    auto code = generateCpp(index, *index.getSymbol(parentId), false, opts);
    EXPECT_NE(code.find("public:"), std::string::npos);
    EXPECT_NE(code.find("private:"), std::string::npos);
    // public section should come before private
    EXPECT_LT(code.find("public:"), code.find("private:"));
}

// ═════════════════════════════════════════════════════════════════════════════
// C code generation
// ═════════════════════════════════════════════════════════════════════════════

TEST(CodeGenC, TypedefStruct)
{
    PdbIndex index;
    auto sym = makeUdt("Vec3");
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("x", "float", 0, 4));
    udt.members.push_back(makeMember("y", "float", 4, 4));
    udt.members.push_back(makeMember("z", "float", 8, 4));
    index.addSymbol(std::move(sym));

    auto code = generateC(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("typedef struct Vec3"), std::string::npos);
    EXPECT_NE(code.find("} Vec3;"), std::string::npos);
    EXPECT_NE(code.find("float x;"), std::string::npos);
}

TEST(CodeGenC, TypedefUnion)
{
    PdbIndex index;
    auto sym = makeUdt("Value", UdtTag::Union);
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("i", "int", 0, 4));
    udt.members.push_back(makeMember("f", "float", 0, 4));
    index.addSymbol(std::move(sym));

    auto code = generateC(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("typedef union Value"), std::string::npos);
    EXPECT_NE(code.find("} Value;"), std::string::npos);
}

TEST(CodeGenC, ArrayMember)
{
    PdbIndex index;
    auto sym = makeUdt("Mesh");
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("verts", "float[3]", 0, 12));
    index.addSymbol(std::move(sym));

    auto code = generateC(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("float verts[3]"), std::string::npos);
}

TEST(CodeGenC, BitfieldMember)
{
    PdbIndex index;
    auto sym = makeUdt("Bits");
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("flag", "unsigned int", 0, 4, 0, 1));
    index.addSymbol(std::move(sym));

    auto code = generateC(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("unsigned int flag : 1;"), std::string::npos);
}

TEST(CodeGenC, TypedefEnum)
{
    PdbIndex index;
    auto sym = makeEnum("Color");
    auto& en = std::get<EnumData>(sym.kindData);
    en.enumValues.push_back({"RED", 0});
    en.enumValues.push_back({"GREEN", 1});
    index.addSymbol(std::move(sym));

    auto code = generateC(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("typedef enum Color"), std::string::npos);
    EXPECT_NE(code.find("} Color;"), std::string::npos);
    EXPECT_NE(code.find("RED = 0"), std::string::npos);
}

TEST(CodeGenC, FunctionStripsAccessAndClassPrefix)
{
    PdbIndex index;
    auto sym = makeFunction("Foo::bar", "public: int __cdecl Foo::bar(float)");
    index.addSymbol(std::move(sym));

    auto code = generateC(index, *index.getSymbol(0), false);
    EXPECT_EQ(code.find("public:"), std::string::npos);
    EXPECT_EQ(code.find("Foo::"), std::string::npos);
    EXPECT_NE(code.find("bar(float)"), std::string::npos);
}

TEST(CodeGenC, ConstData)
{
    PdbIndex index;
    auto sym = makeData("LIMIT", "int", 100);
    index.addSymbol(std::move(sym));

    auto code = generateC(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("const int LIMIT = 100;"), std::string::npos);
}

TEST(CodeGenC, ExternStaticMembers)
{
    PdbIndex index;
    auto parent = makeUdt("Config");
    SymbolId parentId = index.addSymbol(std::move(parent));

    auto child = makeData("s_default", "Config *", 0, false);
    child.pdbId = 1;
    child.kind = SymbolKind::Data;
    SymbolId childId = index.addSymbol(std::move(child));

    auto* parentSym = const_cast<SymbolNode*>(index.getSymbol(parentId));
    parentSym->children.push_back(childId);

    CodeGenOptions opts;
    opts.includeStatics = true;
    auto code = generateC(index, *index.getSymbol(parentId), false, opts);
    EXPECT_NE(code.find("extern"), std::string::npos);
    EXPECT_NE(code.find("s_default"), std::string::npos);
}

TEST(CodeGenC, MemberFunctionsWithClassNamePrefix)
{
    PdbIndex index;
    auto parent = makeUdt("Player");
    SymbolId parentId = index.addSymbol(std::move(parent));

    auto fn = makeFunction("Player::jump", "public: void __cdecl Player::jump(void)");
    fn.pdbId = 1;
    SymbolId fnId = index.addSymbol(std::move(fn));

    auto* parentSym = const_cast<SymbolNode*>(index.getSymbol(parentId));
    parentSym->children.push_back(fnId);

    CodeGenOptions opts;
    opts.includeFunctions = true;
    auto code = generateC(index, *index.getSymbol(parentId), false, opts);
    // Should replace :: with _ for C naming
    EXPECT_NE(code.find("Player_jump"), std::string::npos);
    EXPECT_EQ(code.find("Player::jump"), std::string::npos);
}

// ═════════════════════════════════════════════════════════════════════════════
// Rust code generation
// ═════════════════════════════════════════════════════════════════════════════

TEST(CodeGenRust, ReprCStruct)
{
    PdbIndex index;
    auto sym = makeUdt("Vec2");
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("x", "float", 0, 4));
    udt.members.push_back(makeMember("y", "float", 4, 4));
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("#[repr(C)]"), std::string::npos);
    EXPECT_NE(code.find("pub struct Vec2"), std::string::npos);
    EXPECT_NE(code.find("pub x: float"), std::string::npos);
    EXPECT_NE(code.find("pub y: float"), std::string::npos);
}

TEST(CodeGenRust, Union)
{
    PdbIndex index;
    auto sym = makeUdt("Data", UdtTag::Union);
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("i", "int", 0, 4));
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("pub union Data"), std::string::npos);
}

TEST(CodeGenRust, PointerMember)
{
    PdbIndex index;
    auto sym = makeUdt("Node");
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("next", "Node *", 0, 8));
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("pub next: *mut Node"), std::string::npos);
}

TEST(CodeGenRust, ConstPointerMember)
{
    PdbIndex index;
    auto sym = makeUdt("Ref");
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("data", "const int *", 0, 8));
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("pub data: *const int"), std::string::npos);
}

TEST(CodeGenRust, ReferenceMember)
{
    PdbIndex index;
    auto sym = makeUdt("Wrapper");
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("val", "int &", 0, 8));
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("pub val: &mut int"), std::string::npos);
}

TEST(CodeGenRust, ConstReferenceMember)
{
    PdbIndex index;
    auto sym = makeUdt("View");
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("data", "const int &", 0, 8));
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("pub data: &int"), std::string::npos);
}

TEST(CodeGenRust, ArrayMember)
{
    PdbIndex index;
    auto sym = makeUdt("Mesh");
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("verts", "float[3]", 0, 12));
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("pub verts: [float; 3]"), std::string::npos);
}

TEST(CodeGenRust, DoublePointer)
{
    PdbIndex index;
    auto sym = makeUdt("Table");
    auto& udt = std::get<UdtData>(sym.kindData);
    udt.members.push_back(makeMember("entries", "void * *", 0, 8));
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("pub entries: *mut *mut void"), std::string::npos);
}

TEST(CodeGenRust, EnumWithRepr)
{
    PdbIndex index;
    auto sym = makeEnum("State", "unsigned int");
    auto& en = std::get<EnumData>(sym.kindData);
    en.enumValues.push_back({"Idle", 0});
    en.enumValues.push_back({"Running", 1});
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("#[repr(unsigned int)]"), std::string::npos);
    EXPECT_NE(code.find("pub enum State"), std::string::npos);
    EXPECT_NE(code.find("Idle = 0"), std::string::npos);
    EXPECT_NE(code.find("Running = 1"), std::string::npos);
}

TEST(CodeGenRust, FunctionNoReturnType)
{
    PdbIndex index;
    auto sym = makeFunction("init", "void __cdecl init(void)");
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("fn init()"), std::string::npos);
    EXPECT_EQ(code.find("->"), std::string::npos);
    EXPECT_EQ(code.find("virtual"), std::string::npos);
}

TEST(CodeGenRust, FunctionWithReturnType)
{
    PdbIndex index;
    auto sym = makeFunction("getCount", "int __cdecl getCount(void)");
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("fn getCount()"), std::string::npos);
    EXPECT_NE(code.find("-> int"), std::string::npos);
}

TEST(CodeGenRust, FunctionWithParams)
{
    PdbIndex index;
    auto sym = makeFunction("add", "int __cdecl add(int, int)");
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("fn add(int, int) -> int"), std::string::npos);
}

TEST(CodeGenRust, FunctionStripsVirtual)
{
    PdbIndex index;
    auto sym = makeFunction("Foo::tick", "public: virtual void __cdecl Foo::tick(void)");
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_EQ(code.find("virtual"), std::string::npos);
    EXPECT_NE(code.find("fn tick()"), std::string::npos);
}

TEST(CodeGenRust, FunctionPointerParam)
{
    PdbIndex index;
    auto sym = makeFunction("process", "void __cdecl process(const float *)");
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("*const float"), std::string::npos);
}

TEST(CodeGenRust, FunctionPointerReturnType)
{
    PdbIndex index;
    auto sym = makeFunction("getData", "int * __cdecl getData(void)");
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("-> *mut int"), std::string::npos);
}

TEST(CodeGenRust, ConstData)
{
    PdbIndex index;
    auto sym = makeData("LIMIT", "int", 100);
    index.addSymbol(std::move(sym));

    auto code = generateRust(index, *index.getSymbol(0), false);
    EXPECT_NE(code.find("const LIMIT: int = 100;"), std::string::npos);
}

TEST(CodeGenRust, ImplBlockStatics)
{
    PdbIndex index;
    auto parent = makeUdt("Manager");
    SymbolId parentId = index.addSymbol(std::move(parent));

    auto child = makeData("s_count", "int", 0, false);
    child.pdbId = 1;
    child.kind = SymbolKind::Data;
    child.typeName = "int";
    SymbolId childId = index.addSymbol(std::move(child));

    auto* parentSym = const_cast<SymbolNode*>(index.getSymbol(parentId));
    parentSym->children.push_back(childId);

    CodeGenOptions opts;
    opts.includeStatics = true;
    auto code = generateRust(index, *index.getSymbol(parentId), false, opts);
    EXPECT_NE(code.find("impl Manager {"), std::string::npos);
    EXPECT_NE(code.find("pub static s_count: int"), std::string::npos);
}

TEST(CodeGenRust, ImplBlockMemberFunctions)
{
    PdbIndex index;
    auto parent = makeUdt("Entity");
    SymbolId parentId = index.addSymbol(std::move(parent));

    auto fn = makeFunction("Entity::update", "public: void __cdecl Entity::update(float)");
    fn.pdbId = 1;
    SymbolId fnId = index.addSymbol(std::move(fn));

    auto* parentSym = const_cast<SymbolNode*>(index.getSymbol(parentId));
    parentSym->children.push_back(fnId);

    CodeGenOptions opts;
    opts.includeFunctions = true;
    auto code = generateRust(index, *index.getSymbol(parentId), false, opts);
    EXPECT_NE(code.find("impl Entity {"), std::string::npos);
    EXPECT_NE(code.find("&self"), std::string::npos);
    EXPECT_NE(code.find("float"), std::string::npos);
}

TEST(CodeGenRust, MemberFunctionSelfNotForStatic)
{
    PdbIndex index;
    auto parent = makeUdt("Factory");
    SymbolId parentId = index.addSymbol(std::move(parent));

    auto fn = makeFunction("Factory::create", "public: static Factory * __cdecl Factory::create(void)");
    fn.pdbId = 1;
    SymbolId fnId = index.addSymbol(std::move(fn));

    auto* parentSym = const_cast<SymbolNode*>(index.getSymbol(parentId));
    parentSym->children.push_back(fnId);

    CodeGenOptions opts;
    opts.includeFunctions = true;
    auto code = generateRust(index, *index.getSymbol(parentId), false, opts);
    EXPECT_NE(code.find("impl Factory {"), std::string::npos);
    // Static functions should NOT have &self
    auto fnPos = code.find("fn create");
    ASSERT_NE(fnPos, std::string::npos);
    auto parenEnd = code.find(')', fnPos);
    auto fnSig = code.substr(fnPos, parenEnd - fnPos + 1);
    EXPECT_EQ(fnSig.find("&self"), std::string::npos);
}

// ═════════════════════════════════════════════════════════════════════════════
// Options: no optional sections when flags are off
// ═════════════════════════════════════════════════════════════════════════════

TEST(CodeGenOptions, StaticsNotIncludedByDefault)
{
    PdbIndex index;
    auto parent = makeUdt("Foo");
    SymbolId parentId = index.addSymbol(std::move(parent));

    auto child = makeData("s_data", "int", 0, false);
    child.pdbId = 1;
    child.kind = SymbolKind::Data;
    SymbolId childId = index.addSymbol(std::move(child));

    auto* parentSym = const_cast<SymbolNode*>(index.getSymbol(parentId));
    parentSym->children.push_back(childId);

    // Default opts: includeStatics=false
    auto codeCpp = generateCpp(index, *index.getSymbol(parentId), false);
    auto codeC   = generateC(index, *index.getSymbol(parentId), false);
    auto codeRs  = generateRust(index, *index.getSymbol(parentId), false);

    EXPECT_EQ(codeCpp.find("s_data"), std::string::npos);
    EXPECT_EQ(codeC.find("s_data"), std::string::npos);
    EXPECT_EQ(codeRs.find("s_data"), std::string::npos);
}

TEST(CodeGenOptions, FunctionsNotIncludedByDefault)
{
    PdbIndex index;
    auto parent = makeUdt("Bar");
    SymbolId parentId = index.addSymbol(std::move(parent));

    auto fn = makeFunction("Bar::doIt", "public: void __cdecl Bar::doIt(void)");
    fn.pdbId = 1;
    SymbolId fnId = index.addSymbol(std::move(fn));

    auto* parentSym = const_cast<SymbolNode*>(index.getSymbol(parentId));
    parentSym->children.push_back(fnId);

    auto codeCpp = generateCpp(index, *index.getSymbol(parentId), false);
    auto codeC   = generateC(index, *index.getSymbol(parentId), false);
    auto codeRs  = generateRust(index, *index.getSymbol(parentId), false);

    EXPECT_EQ(codeCpp.find("doIt"), std::string::npos);
    EXPECT_EQ(codeC.find("doIt"), std::string::npos);
    EXPECT_EQ(codeRs.find("doIt"), std::string::npos);
}

// ═════════════════════════════════════════════════════════════════════════════
// Unsupported kind returns empty
// ═════════════════════════════════════════════════════════════════════════════

TEST(CodeGen, UnsupportedKindReturnsEmpty)
{
    PdbIndex index;
    SymbolNode sym;
    sym.pdbId = 0;
    sym.kind = SymbolKind::Typedef;
    index.addSymbol(std::move(sym));

    EXPECT_TRUE(generateCpp(index, *index.getSymbol(0), false).empty());
    EXPECT_TRUE(generateC(index, *index.getSymbol(0), false).empty());
    EXPECT_TRUE(generateRust(index, *index.getSymbol(0), false).empty());
}

TEST(CodeGen, DataWithoutConstValueReturnsEmpty)
{
    PdbIndex index;
    auto sym = makeData("g_var", "int", 0, /*hasConst=*/false);
    index.addSymbol(std::move(sym));

    EXPECT_TRUE(generateCpp(index, *index.getSymbol(0), false).empty());
    EXPECT_TRUE(generateC(index, *index.getSymbol(0), false).empty());
    EXPECT_TRUE(generateRust(index, *index.getSymbol(0), false).empty());
}
