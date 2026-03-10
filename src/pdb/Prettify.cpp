#include "Prettify.h"
#include "PrettifyCpp.h"
#include "PrettifyRust.h"
#include "SymbolNode.h"

#include <string>
#include <string_view>

// ── Public API ──────────────────────────────────────────────────────────────

std::string prettifyName(std::string_view raw)
{
    if (raw.empty()) return {};

    // Apply both language rulesets — they don't conflict.
    std::string s = prettifyCpp(raw);
    s = prettifyRust(s);

    return s;
}

std::string_view displayName(const SymbolNode& sym, bool prettify)
{
    if (prettify && !sym.prettyName.empty())
        return sym.prettyName;

    // Fallback: prefer undecoratedName for functions
    const std::string& raw =
        sym.undecoratedName.empty() ? sym.name : sym.undecoratedName;

    if (raw.empty())
        return "<unnamed>";

    return raw;
}

std::string_view displayTypeName(std::string_view rawTypeName,
                                 std::string_view prettyTypeName,
                                 bool prettify)
{
    if (prettify && !prettyTypeName.empty())
        return prettyTypeName;
    return rawTypeName;
}
