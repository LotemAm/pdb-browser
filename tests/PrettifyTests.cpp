#include <gtest/gtest.h>

#include "pdb/Prettify.h"
#include "pdb/PrettifyCpp.h"
#include "pdb/PrettifyRust.h"
#include "pdb/SymbolNode.h"

// ═════════════════════════════════════════════════════════════════════════════
// prettifyCpp
// ═════════════════════════════════════════════════════════════════════════════

// ── Lambda cleanup ──────────────────────────────────────────────────────────

TEST(PrettifyCpp, LambdaHexReplacedWithLambda)
{
    EXPECT_EQ(prettifyCpp("foo::<lambda_abc123>"), "foo::<lambda>");
}

TEST(PrettifyCpp, MultipleLambdasReplaced)
{
    EXPECT_EQ(prettifyCpp("A::<lambda_1a>::B::<lambda_2b>"),
              "A::<lambda>::B::<lambda>");
}

TEST(PrettifyCpp, LambdaWithoutHexUnchanged)
{
    // No hex digits after "lambda_" — should not match
    EXPECT_EQ(prettifyCpp("<lambda_>"), "<lambda_>");
}

// ── Calling convention removal ──────────────────────────────────────────────

TEST(PrettifyCpp, RemovesCdecl)
{
    EXPECT_EQ(prettifyCpp("void __cdecl foo()"), "void foo()");
}

TEST(PrettifyCpp, RemovesStdcall)
{
    EXPECT_EQ(prettifyCpp("int __stdcall bar(int)"), "int bar(int)");
}

TEST(PrettifyCpp, RemovesFastcall)
{
    EXPECT_EQ(prettifyCpp("void __fastcall baz()"), "void baz()");
}

TEST(PrettifyCpp, RemovesThiscall)
{
    EXPECT_EQ(prettifyCpp("void __thiscall Foo::bar()"), "void Foo::bar()");
}

TEST(PrettifyCpp, RemovesVectorcall)
{
    EXPECT_EQ(prettifyCpp("void __vectorcall calc()"), "void calc()");
}

// ── Type qualifier removal ──────────────────────────────────────────────────

TEST(PrettifyCpp, RemovesClassQualifier)
{
    EXPECT_EQ(prettifyCpp("void foo(class Foo)"), "void foo(Foo)");
}

TEST(PrettifyCpp, RemovesStructQualifier)
{
    EXPECT_EQ(prettifyCpp("struct Foo *p"), "Foo *p");
}

TEST(PrettifyCpp, RemovesEnumQualifier)
{
    EXPECT_EQ(prettifyCpp("enum Color c"), "Color c");
}

TEST(PrettifyCpp, RemovesQualifierInTemplate)
{
    EXPECT_EQ(prettifyCpp("std::vector<class Foo>"), "std::vector<Foo>");
}

TEST(PrettifyCpp, DoesNotRemoveQualifierMidWord)
{
    // "rclass " shouldn't trigger removal — only at boundaries
    EXPECT_EQ(prettifyCpp("rclass Foo"), "rclass Foo");
}

// ── std:: type collapsing ───────────────────────────────────────────────────

TEST(PrettifyCpp, CollapsesBasicStringToString)
{
    EXPECT_EQ(
        prettifyCpp("std::basic_string<char,std::char_traits<char>,std::allocator<char>>"),
        "std::string");
}

TEST(PrettifyCpp, CollapsesBasicStringWithSpaces)
{
    EXPECT_EQ(
        prettifyCpp("std::basic_string<char, std::char_traits<char>, std::allocator<char>>"),
        "std::string");
}

TEST(PrettifyCpp, CollapsesWstring)
{
    EXPECT_EQ(
        prettifyCpp("std::basic_string<wchar_t,std::char_traits<wchar_t>,std::allocator<wchar_t>>"),
        "std::wstring");
}

TEST(PrettifyCpp, CollapsesStringView)
{
    EXPECT_EQ(
        prettifyCpp("std::basic_string_view<char,std::char_traits<char>>"),
        "std::string_view");
}

TEST(PrettifyCpp, CollapsesOstream)
{
    EXPECT_EQ(
        prettifyCpp("std::basic_ostream<char,std::char_traits<char>>"),
        "std::ostream");
}

TEST(PrettifyCpp, CollapsesIstream)
{
    EXPECT_EQ(
        prettifyCpp("std::basic_istream<char,std::char_traits<char>>"),
        "std::istream");
}

// ── Default template arg collapsing ─────────────────────────────────────────

TEST(PrettifyCpp, CollapsesVectorAllocator)
{
    EXPECT_EQ(
        prettifyCpp("std::vector<int,std::allocator<int>>"),
        "std::vector<int>");
}

TEST(PrettifyCpp, CollapsesVectorAllocatorWithSpaces)
{
    EXPECT_EQ(
        prettifyCpp("std::vector<int, std::allocator<int>>"),
        "std::vector<int>");
}

TEST(PrettifyCpp, CollapsesListAllocator)
{
    EXPECT_EQ(
        prettifyCpp("std::list<double, std::allocator<double>>"),
        "std::list<double>");
}

TEST(PrettifyCpp, CollapsesDequeAllocator)
{
    EXPECT_EQ(
        prettifyCpp("std::deque<int, std::allocator<int>>"),
        "std::deque<int>");
}

TEST(PrettifyCpp, CollapsesUniquePtrDefaultDelete)
{
    EXPECT_EQ(
        prettifyCpp("std::unique_ptr<Foo,std::default_delete<Foo>>"),
        "std::unique_ptr<Foo>");
}

TEST(PrettifyCpp, CollapsesSetDefaultArgs)
{
    EXPECT_EQ(
        prettifyCpp("std::set<int,std::less<int>,std::allocator<int>>"),
        "std::set<int>");
}

TEST(PrettifyCpp, CollapsesMapDefaultArgs)
{
    // DIA uses east-const "K const"
    EXPECT_EQ(
        prettifyCpp("std::map<int,float,std::less<int>,std::allocator<std::pair<int const,float>>>"),
        "std::map<int, float>");
}

TEST(PrettifyCpp, VectorWithSingleArgUnchanged)
{
    EXPECT_EQ(prettifyCpp("std::vector<int>"), "std::vector<int>");
}

// ── Whitespace normalization ────────────────────────────────────────────────

TEST(PrettifyCpp, CollapsesMultipleSpaces)
{
    EXPECT_EQ(prettifyCpp("void   foo(  int  )"), "void foo(int)");
}

TEST(PrettifyCpp, RemovesSpaceBeforeClosingAngle)
{
    EXPECT_EQ(prettifyCpp("vector<int >"), "vector<int>");
}

TEST(PrettifyCpp, RemovesSpaceAfterOpeningAngle)
{
    EXPECT_EQ(prettifyCpp("vector< int>"), "vector<int>");
}

TEST(PrettifyCpp, TrimsLeadingTrailingWhitespace)
{
    EXPECT_EQ(prettifyCpp("  foo  "), "foo");
}

// ── Empty / identity ────────────────────────────────────────────────────────

TEST(PrettifyCpp, EmptyStringReturnsEmpty)
{
    EXPECT_EQ(prettifyCpp(""), "");
}

TEST(PrettifyCpp, PlainNameUnchanged)
{
    EXPECT_EQ(prettifyCpp("MyClass::myMethod"), "MyClass::myMethod");
}

// ── Combined passes ─────────────────────────────────────────────────────────

TEST(PrettifyCpp, CombinedCallingConvAndQualifiers)
{
    EXPECT_EQ(
        prettifyCpp("void __cdecl foo(class Foo, struct Bar)"),
        "void foo(Foo, Bar)");
}

TEST(PrettifyCpp, CombinedAllPasses)
{
    // Lambda + calling conv + qualifier + std::string + vector collapse
    auto input = "void __thiscall class Foo::<lambda_aabb>::operator()(std::basic_string<char,std::char_traits<char>,std::allocator<char>>,std::vector<int,std::allocator<int>>)";
    auto result = prettifyCpp(input);
    EXPECT_EQ(result, "void Foo::<lambda>::operator()(std::string, std::vector<int>)");
}

// ═════════════════════════════════════════════════════════════════════════════
// prettifyRust
// ═════════════════════════════════════════════════════════════════════════════

// ── alloc:: path shortening ─────────────────────────────────────────────────

TEST(PrettifyRust, ShortensAllocString)
{
    EXPECT_EQ(prettifyRust("alloc::string::String"), "String");
}

TEST(PrettifyRust, ShortensAllocVec)
{
    EXPECT_EQ(prettifyRust("alloc::vec::Vec"), "Vec");
}

TEST(PrettifyRust, ShortensAllocBox)
{
    EXPECT_EQ(prettifyRust("alloc::boxed::Box"), "Box");
}

TEST(PrettifyRust, ShortensAllocRc)
{
    EXPECT_EQ(prettifyRust("alloc::rc::Rc"), "Rc");
}

TEST(PrettifyRust, ShortensAllocArc)
{
    EXPECT_EQ(prettifyRust("alloc::sync::Arc"), "Arc");
}

// ── core:: path shortening ──────────────────────────────────────────────────

TEST(PrettifyRust, ShortensCoreOption)
{
    EXPECT_EQ(prettifyRust("core::option::Option"), "Option");
}

TEST(PrettifyRust, ShortensCoreResult)
{
    EXPECT_EQ(prettifyRust("core::result::Result"), "Result");
}

TEST(PrettifyRust, ShortensCoreFuture)
{
    EXPECT_EQ(prettifyRust("core::future::Future"), "Future");
}

TEST(PrettifyRust, ShortensCoreDisplay)
{
    EXPECT_EQ(prettifyRust("core::fmt::Display"), "Display");
}

TEST(PrettifyRust, ShortensCorePin)
{
    EXPECT_EQ(prettifyRust("core::pin::Pin"), "Pin");
}

// ── closure$/impl$ cleanup ──────────────────────────────────────────────────

TEST(PrettifyRust, CleansClosureArtifact)
{
    EXPECT_EQ(prettifyRust("foo::closure$0"), "foo::<closure>");
}

TEST(PrettifyRust, CleansMultipleClosures)
{
    EXPECT_EQ(prettifyRust("a::closure$1::b::closure$2"),
              "a::<closure>::b::<closure>");
}

TEST(PrettifyRust, CleansImplArtifact)
{
    EXPECT_EQ(prettifyRust("foo::impl$0::bar"), "foo::impl::bar");
}

TEST(PrettifyRust, CleansMultiDigitImpl)
{
    EXPECT_EQ(prettifyRust("impl$123"), "impl");
}

// ── std:: (Rust) path shortening ────────────────────────────────────────────

TEST(PrettifyRust, ShortensStdHashMap)
{
    EXPECT_EQ(prettifyRust("std::collections::HashMap"), "HashMap");
}

TEST(PrettifyRust, ShortensStdMutex)
{
    EXPECT_EQ(prettifyRust("std::sync::Mutex"), "Mutex");
}

TEST(PrettifyRust, ShortensStdPath)
{
    EXPECT_EQ(prettifyRust("std::path::Path"), "Path");
}

TEST(PrettifyRust, ShortensStdIoError)
{
    EXPECT_EQ(prettifyRust("std::io::Error"), "io::Error");
}

// ── Boundary safety ─────────────────────────────────────────────────────────

TEST(PrettifyRust, DoesNotShortenNonBoundaryMatch)
{
    // "xalloc::string::String" — not at a path boundary
    EXPECT_EQ(prettifyRust("xalloc::string::String"), "xalloc::string::String");
}

TEST(PrettifyRust, ShortensInTemplateContext)
{
    EXPECT_EQ(prettifyRust("Vec<core::option::Option<alloc::string::String>>"),
              "Vec<Option<String>>");
}

// ── Empty / identity ────────────────────────────────────────────────────────

TEST(PrettifyRust, EmptyStringReturnsEmpty)
{
    EXPECT_EQ(prettifyRust(""), "");
}

TEST(PrettifyRust, PlainRustPathUnchanged)
{
    EXPECT_EQ(prettifyRust("my_crate::module::Type"), "my_crate::module::Type");
}

// ═════════════════════════════════════════════════════════════════════════════
// prettifyName (combined C++ + Rust)
// ═════════════════════════════════════════════════════════════════════════════

TEST(PrettifyName, AppliesBothRulesets)
{
    // C++ rule: remove __cdecl; Rust rule: shorten core::option::Option
    EXPECT_EQ(
        prettifyName("void __cdecl foo(core::option::Option)"),
        "void foo(Option)");
}

TEST(PrettifyName, EmptyReturnsEmpty)
{
    EXPECT_EQ(prettifyName(""), "");
}

// ═════════════════════════════════════════════════════════════════════════════
// displayName
// ═════════════════════════════════════════════════════════════════════════════

TEST(DisplayName, ReturnsPrettyNameWhenEnabled)
{
    SymbolNode sym;
    sym.name = "raw_name";
    sym.undecoratedName = "undecorated";
    sym.prettyName = "pretty";

    EXPECT_EQ(displayName(sym, true), "pretty");
}

TEST(DisplayName, FallsBackToUndecoratedWhenPrettifyOff)
{
    SymbolNode sym;
    sym.name = "raw_name";
    sym.undecoratedName = "undecorated";
    sym.prettyName = "pretty";

    EXPECT_EQ(displayName(sym, false), "undecorated");
}

TEST(DisplayName, FallsBackToNameWhenNoUndecorated)
{
    SymbolNode sym;
    sym.name = "raw_name";

    EXPECT_EQ(displayName(sym, false), "raw_name");
}

TEST(DisplayName, FallsBackToUndecoratedWhenPrettyEmpty)
{
    SymbolNode sym;
    sym.name = "raw_name";
    sym.undecoratedName = "undecorated";

    EXPECT_EQ(displayName(sym, true), "undecorated");
}

TEST(DisplayName, ReturnsUnnamedWhenAllEmpty)
{
    SymbolNode sym;
    EXPECT_EQ(displayName(sym, false), "<unnamed>");
}

// ═════════════════════════════════════════════════════════════════════════════
// displayTypeName
// ═════════════════════════════════════════════════════════════════════════════

TEST(DisplayTypeName, ReturnsPrettyWhenEnabled)
{
    EXPECT_EQ(displayTypeName("raw", "pretty", true), "pretty");
}

TEST(DisplayTypeName, ReturnsRawWhenPrettifyOff)
{
    EXPECT_EQ(displayTypeName("raw", "pretty", false), "raw");
}

TEST(DisplayTypeName, ReturnsRawWhenPrettyEmpty)
{
    EXPECT_EQ(displayTypeName("raw", "", true), "raw");
}
