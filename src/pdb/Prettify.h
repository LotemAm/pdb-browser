#pragma once
#include <string>
#include <string_view>

struct SymbolNode;

// Apply all prettification rules (C++ and Rust) to a raw symbol name.
// Called at symbol load time to pre-compute prettyName / prettyTypeName.
std::string prettifyName(std::string_view raw);

// Return the best display name for a symbol.
// When prettify == false: returns undecoratedName if available, else name.
// When prettify == true:  returns pre-computed prettyName.
std::string_view displayName(const SymbolNode& sym, bool prettify);

// Pick between raw and pretty type name strings.
// When prettify == true and prettyTypeName is non-empty, returns prettyTypeName.
std::string_view displayTypeName(std::string_view rawTypeName,
                                 std::string_view prettyTypeName,
                                 bool prettify);

// Strip leading "public: ", "private: ", "protected: " from DIA undecorated names.
std::string stripAccessSpecifiers(std::string_view s);

// Strip access specifiers plus "virtual " and "static " prefixes.
std::string stripFunctionQualifiers(std::string_view s);
