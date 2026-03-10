#include "PrettifyCpp.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

// ── Helpers ──────────────────────────────────────────────────────────────────

// Find the end of a template argument starting at `pos`, respecting nested <>.
// Returns the position of the terminating ',' or '>' at depth 0, or npos.
static size_t findTemplateArgEnd(const std::string& s, size_t pos)
{
    int depth = 0;
    for (size_t i = pos; i < s.size(); ++i) {
        char c = s[i];
        if (c == '<') ++depth;
        else if (c == '>') {
            if (depth == 0) return i;
            --depth;
        }
        else if (c == ',' && depth == 0) return i;
    }
    return std::string::npos;
}

// Extract template argument text from `pos` to the next ',' or '>' at depth 0.
// Returns the trimmed argument text. Advances `pos` past the argument and delimiter.
static std::string_view extractTemplateArg(const std::string& s, size_t& pos)
{
    size_t end = findTemplateArgEnd(s, pos);
    if (end == std::string::npos) {
        auto arg = std::string_view{s}.substr(pos);
        pos = s.size();
        return arg;
    }
    auto arg = std::string_view{s}.substr(pos, end - pos);
    // Trim leading/trailing whitespace
    while (!arg.empty() && arg.front() == ' ') arg.remove_prefix(1);
    while (!arg.empty() && arg.back() == ' ')  arg.remove_suffix(1);
    pos = end; // caller decides whether to skip ',' or '>'
    return arg;
}

// Remove all non-overlapping occurrences of `pattern` from `s`.
static void removeAll(std::string& s, std::string_view pattern)
{
    size_t pos = 0;
    while ((pos = s.find(pattern, pos)) != std::string::npos)
        s.erase(pos, pattern.size());
}

// Replace all non-overlapping occurrences of `from` with `to` in `s`.
static void replaceAll(std::string& s, std::string_view from, std::string_view to)
{
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

// ── Pass 1: Lambda cleanup ──────────────────────────────────────────────────

static void replaceLambdaPatterns(std::string& s)
{
    // Replace <lambda_HEXDIGITS> with <lambda>
    constexpr std::string_view prefix = "<lambda_";
    size_t pos = 0;
    while ((pos = s.find(prefix, pos)) != std::string::npos) {
        size_t hexStart = pos + prefix.size();
        size_t hexEnd = hexStart;
        while (hexEnd < s.size() && std::isxdigit(static_cast<unsigned char>(s[hexEnd])))
            ++hexEnd;
        if (hexEnd > hexStart && hexEnd < s.size() && s[hexEnd] == '>') {
            s.replace(pos, hexEnd - pos + 1, "<lambda>");
            pos += 8; // length of "<lambda>"
        } else {
            pos = hexStart;
        }
    }
}

// ── Pass 2: Remove calling conventions ──────────────────────────────────────

static void removeCallingConventions(std::string& s)
{
    static constexpr std::array conventions = {
        std::string_view{"__cdecl "},
        std::string_view{"__stdcall "},
        std::string_view{"__fastcall "},
        std::string_view{"__thiscall "},
        std::string_view{"__vectorcall "},
    };
    for (auto conv : conventions)
        removeAll(s, conv);
}

// ── Pass 3: Remove class/struct/enum qualifiers ─────────────────────────────

static void removeTypeQualifiers(std::string& s)
{
    // Remove "class ", "struct ", "enum " when they appear as type qualifiers:
    // after '<', ',', '(', or at start of string, followed by an identifier char.
    static constexpr std::array qualifiers = {
        std::string_view{"class "},
        std::string_view{"struct "},
        std::string_view{"enum "},
    };

    for (auto qual : qualifiers) {
        size_t pos = 0;
        while ((pos = s.find(qual, pos)) != std::string::npos) {
            bool atBoundary = (pos == 0);
            if (!atBoundary) {
                char prev = s[pos - 1];
                atBoundary = (prev == '<' || prev == ',' || prev == '(' || prev == ' ');
            }
            if (atBoundary) {
                s.erase(pos, qual.size());
            } else {
                pos += qual.size();
            }
        }
    }
}

// ── Pass 4: Collapse well-known std:: types (exact patterns) ────────────────

static void collapseExactStdTypes(std::string& s)
{
    // Order matters: more specific patterns first.
    // Handle with and without spaces after commas.
    struct Replacement {
        std::string_view from;
        std::string_view to;
    };

    // basic_string variants
    static constexpr Replacement stringReplacements[] = {
        // char version — with spaces
        {"std::basic_string<char,std::char_traits<char>,std::allocator<char>>", "std::string"},
        {"std::basic_string<char, std::char_traits<char>, std::allocator<char>>", "std::string"},
        {"std::basic_string<char,std::char_traits<char> ,std::allocator<char> >", "std::string"},
        {"std::basic_string<char, std::char_traits<char> , std::allocator<char> >", "std::string"},
        // wchar_t version
        {"std::basic_string<wchar_t,std::char_traits<wchar_t>,std::allocator<wchar_t>>", "std::wstring"},
        {"std::basic_string<wchar_t, std::char_traits<wchar_t>, std::allocator<wchar_t>>", "std::wstring"},
        {"std::basic_string<wchar_t,std::char_traits<wchar_t> ,std::allocator<wchar_t> >", "std::wstring"},
        {"std::basic_string<wchar_t, std::char_traits<wchar_t> , std::allocator<wchar_t> >", "std::wstring"},
        // char16_t version
        {"std::basic_string<char16_t,std::char_traits<char16_t>,std::allocator<char16_t>>", "std::u16string"},
        {"std::basic_string<char16_t, std::char_traits<char16_t>, std::allocator<char16_t>>", "std::u16string"},
        // char32_t version
        {"std::basic_string<char32_t,std::char_traits<char32_t>,std::allocator<char32_t>>", "std::u32string"},
        {"std::basic_string<char32_t, std::char_traits<char32_t>, std::allocator<char32_t>>", "std::u32string"},
        // char8_t version (C++20)
        {"std::basic_string<char8_t,std::char_traits<char8_t>,std::allocator<char8_t>>", "std::u8string"},
        {"std::basic_string<char8_t, std::char_traits<char8_t>, std::allocator<char8_t>>", "std::u8string"},
    };

    // basic_string_view variants
    static constexpr Replacement svReplacements[] = {
        {"std::basic_string_view<char,std::char_traits<char>>", "std::string_view"},
        {"std::basic_string_view<char, std::char_traits<char>>", "std::string_view"},
        {"std::basic_string_view<char,std::char_traits<char> >", "std::string_view"},
        {"std::basic_string_view<char, std::char_traits<char> >", "std::string_view"},
        {"std::basic_string_view<wchar_t,std::char_traits<wchar_t>>", "std::wstring_view"},
        {"std::basic_string_view<wchar_t, std::char_traits<wchar_t>>", "std::wstring_view"},
    };

    // iostream variants
    static constexpr Replacement streamReplacements[] = {
        {"std::basic_ostream<char,std::char_traits<char>>", "std::ostream"},
        {"std::basic_ostream<char, std::char_traits<char>>", "std::ostream"},
        {"std::basic_ostream<char,std::char_traits<char> >", "std::ostream"},
        {"std::basic_ostream<char, std::char_traits<char> >", "std::ostream"},
        {"std::basic_istream<char,std::char_traits<char>>", "std::istream"},
        {"std::basic_istream<char, std::char_traits<char>>", "std::istream"},
        {"std::basic_istream<char,std::char_traits<char> >", "std::istream"},
        {"std::basic_istream<char, std::char_traits<char> >", "std::istream"},
        {"std::basic_iostream<char,std::char_traits<char>>", "std::iostream"},
        {"std::basic_iostream<char, std::char_traits<char>>", "std::iostream"},
        {"std::basic_streambuf<char,std::char_traits<char>>", "std::streambuf"},
        {"std::basic_streambuf<char, std::char_traits<char>>", "std::streambuf"},
        {"std::basic_stringbuf<char,std::char_traits<char>,std::allocator<char>>", "std::stringbuf"},
        {"std::basic_stringbuf<char, std::char_traits<char>, std::allocator<char>>", "std::stringbuf"},
        {"std::basic_ifstream<char, std::char_traits<char>>", "std::ifstream"},
        {"std::basic_ofstream<char, std::char_traits<char>>", "std::ofstream"},
        {"std::basic_fstream<char, std::char_traits<char>>", "std::fstream"},
        {"std::basic_ifstream<wchar_t, std::char_traits<wchar_t>>", "std::wifstream"},
        {"std::basic_ofstream<wchar_t, std::char_traits<wchar_t>>", "std::wofstream"},
        {"std::basic_fstream<wchar_t, std::char_traits<wchar_t>>", "std::wfstream"},
    };

    auto apply = [&](auto& table) {
        for (auto& [from, to] : table)
            replaceAll(s, from, to);
    };

    apply(stringReplacements);
    apply(svReplacements);
    apply(streamReplacements);
}

// ── Pass 5: Collapse default template args ──────────────────────────────────

// Try to collapse `std::ContainerName<T, std::allocator<T>>` → `std::ContainerName<T>`
// for vector, list, deque, forward_list.
static void collapseSimpleAllocator(std::string& s, std::string_view containerPrefix)
{
    size_t pos = 0;
    while ((pos = s.find(containerPrefix, pos)) != std::string::npos) {
        size_t argsStart = pos + containerPrefix.size();
        if (argsStart >= s.size() || s[argsStart] != '<') { pos = argsStart; continue; }
        ++argsStart; // skip '<'

        // Extract first arg T
        size_t argPos = argsStart;
        auto firstArg = extractTemplateArg(s, argPos);
        if (firstArg.empty() || argPos >= s.size()) { pos = argsStart; continue; }

        if (s[argPos] == '>') {
            // Only one arg — already collapsed
            pos = argPos + 1;
            continue;
        }
        if (s[argPos] != ',') { pos = argsStart; continue; }
        ++argPos; // skip ','
        while (argPos < s.size() && s[argPos] == ' ') ++argPos; // skip space

        // Check if second arg is std::allocator<T>
        std::string expectedAlloc = "std::allocator<";
        expectedAlloc += firstArg;
        expectedAlloc += '>';
        auto remaining = std::string_view{s}.substr(argPos);
        if (remaining.starts_with(expectedAlloc)) {
            size_t afterAlloc = argPos + expectedAlloc.size();
            // Skip optional space before '>'
            while (afterAlloc < s.size() && s[afterAlloc] == ' ') ++afterAlloc;
            if (afterAlloc < s.size() && s[afterAlloc] == '>') {
                // Collapse: erase from end of first arg to before final '>'
                size_t firstArgEnd = findTemplateArgEnd(s, argsStart);
                s.erase(firstArgEnd, afterAlloc - firstArgEnd);
                pos = firstArgEnd + 1;
                continue;
            }
        }
        pos = argsStart;
    }
}

// Collapse unique_ptr<T, std::default_delete<T>> → unique_ptr<T>
static void collapseUniquePtr(std::string& s)
{
    constexpr std::string_view prefix = "std::unique_ptr<";
    size_t pos = 0;
    while ((pos = s.find(prefix, pos)) != std::string::npos) {
        size_t argsStart = pos + prefix.size();

        size_t argPos = argsStart;
        auto firstArg = extractTemplateArg(s, argPos);
        if (firstArg.empty() || argPos >= s.size() || s[argPos] != ',') {
            pos = argsStart;
            continue;
        }
        ++argPos;
        while (argPos < s.size() && s[argPos] == ' ') ++argPos;

        std::string expectedDeleter = "std::default_delete<";
        expectedDeleter += firstArg;
        expectedDeleter += '>';
        auto remaining = std::string_view{s}.substr(argPos);
        if (remaining.starts_with(expectedDeleter)) {
            size_t afterDeleter = argPos + expectedDeleter.size();
            while (afterDeleter < s.size() && s[afterDeleter] == ' ') ++afterDeleter;
            if (afterDeleter < s.size() && s[afterDeleter] == '>') {
                size_t firstArgEnd = findTemplateArgEnd(s, argsStart);
                s.erase(firstArgEnd, afterDeleter - firstArgEnd);
                pos = firstArgEnd + 1;
                continue;
            }
        }
        pos = argsStart;
    }
}

// Collapse set<K, std::less<K>, std::allocator<K>> → set<K>
static void collapseSet(std::string& s, std::string_view setPrefix)
{
    size_t pos = 0;
    while ((pos = s.find(setPrefix, pos)) != std::string::npos) {
        size_t argsStart = pos + setPrefix.size();
        if (argsStart >= s.size() || s[argsStart] != '<') { pos = argsStart; continue; }
        ++argsStart;

        size_t argPos = argsStart;
        auto keyArg = extractTemplateArg(s, argPos);
        if (keyArg.empty() || argPos >= s.size() || s[argPos] != ',') {
            pos = argsStart;
            continue;
        }
        ++argPos;
        while (argPos < s.size() && s[argPos] == ' ') ++argPos;

        // Check for std::less<K>
        std::string expectedLess = "std::less<";
        expectedLess += keyArg;
        expectedLess += '>';
        auto remaining = std::string_view{s}.substr(argPos);
        if (!remaining.starts_with(expectedLess)) { pos = argsStart; continue; }
        size_t afterLess = argPos + expectedLess.size();
        // expect , std::allocator<K>>
        while (afterLess < s.size() && s[afterLess] == ' ') ++afterLess;
        if (afterLess >= s.size() || s[afterLess] != ',') { pos = argsStart; continue; }
        ++afterLess;
        while (afterLess < s.size() && s[afterLess] == ' ') ++afterLess;

        std::string expectedAlloc = "std::allocator<";
        expectedAlloc += keyArg;
        expectedAlloc += '>';
        remaining = std::string_view{s}.substr(afterLess);
        if (!remaining.starts_with(expectedAlloc)) { pos = argsStart; continue; }
        size_t afterAlloc = afterLess + expectedAlloc.size();
        while (afterAlloc < s.size() && s[afterAlloc] == ' ') ++afterAlloc;
        if (afterAlloc >= s.size() || s[afterAlloc] != '>') { pos = argsStart; continue; }

        size_t keyArgEnd = findTemplateArgEnd(s, argsStart);
        s.erase(keyArgEnd, afterAlloc - keyArgEnd);
        pos = keyArgEnd + 1;
    }
}

// Collapse map<K, V, std::less<K>, std::allocator<std::pair<K const, V>>> → map<K, V>
static void collapseMap(std::string& s, std::string_view mapPrefix)
{
    size_t pos = 0;
    while ((pos = s.find(mapPrefix, pos)) != std::string::npos) {
        size_t argsStart = pos + mapPrefix.size();
        if (argsStart >= s.size() || s[argsStart] != '<') { pos = argsStart; continue; }
        ++argsStart;

        // Extract K
        size_t argPos = argsStart;
        auto keyArg = extractTemplateArg(s, argPos);
        if (keyArg.empty() || argPos >= s.size() || s[argPos] != ',') {
            pos = argsStart; continue;
        }
        ++argPos;
        while (argPos < s.size() && s[argPos] == ' ') ++argPos;

        // Extract V
        auto valArg = extractTemplateArg(s, argPos);
        if (valArg.empty() || argPos >= s.size()) {
            pos = argsStart; continue;
        }
        if (s[argPos] == '>') {
            // Already only K,V
            pos = argPos + 1; continue;
        }
        if (s[argPos] != ',') { pos = argsStart; continue; }
        ++argPos;
        while (argPos < s.size() && s[argPos] == ' ') ++argPos;

        // Check for std::less<K>
        std::string expectedLess = "std::less<";
        expectedLess += keyArg;
        expectedLess += '>';
        auto remaining = std::string_view{s}.substr(argPos);
        if (!remaining.starts_with(expectedLess)) { pos = argsStart; continue; }
        size_t afterLess = argPos + expectedLess.size();
        while (afterLess < s.size() && s[afterLess] == ' ') ++afterLess;
        if (afterLess >= s.size() || s[afterLess] != ',') { pos = argsStart; continue; }
        ++afterLess;
        while (afterLess < s.size() && s[afterLess] == ' ') ++afterLess;

        // Check for std::allocator<std::pair<K const,V>> — handle "K const" or "const K"
        // DIA typically produces "K const" (east const)
        std::string expectedAllocA = "std::allocator<std::pair<";
        expectedAllocA += keyArg;
        expectedAllocA += " const,";
        std::string expectedAllocB = "std::allocator<std::pair<";
        expectedAllocB += keyArg;
        expectedAllocB += " const, ";
        std::string expectedAllocC = "std::allocator<std::pair<const ";
        expectedAllocC += keyArg;
        expectedAllocC += ',';

        remaining = std::string_view{s}.substr(afterLess);
        size_t allocPrefixLen = 0;
        if (remaining.starts_with(expectedAllocB))      allocPrefixLen = expectedAllocB.size();
        else if (remaining.starts_with(expectedAllocA))  allocPrefixLen = expectedAllocA.size();
        else if (remaining.starts_with(expectedAllocC))  allocPrefixLen = expectedAllocC.size();
        else { pos = argsStart; continue; }

        // Now we need to find V after the prefix, then >>>
        size_t valStart = afterLess + allocPrefixLen;
        while (valStart < s.size() && s[valStart] == ' ') ++valStart;
        size_t valEnd = findTemplateArgEnd(s, valStart);
        if (valEnd == std::string::npos) { pos = argsStart; continue; }
        auto allocVal = std::string_view{s}.substr(valStart, valEnd - valStart);
        while (!allocVal.empty() && allocVal.back() == ' ') allocVal.remove_suffix(1);

        if (allocVal != valArg) { pos = argsStart; continue; }

        // Skip >>> or > > >
        size_t trail = valEnd;
        int closesNeeded = 3; // pair>, allocator>, outer>
        while (trail < s.size() && closesNeeded > 0) {
            if (s[trail] == '>') --closesNeeded;
            else if (s[trail] != ' ') break;
            ++trail;
        }
        if (closesNeeded != 0) { pos = argsStart; continue; }

        // Erase from after V to before the final '>'
        size_t valArgEnd = findTemplateArgEnd(s, argsStart);
        // valArgEnd points to ',' after K. Skip to end of V.
        valArgEnd = argsStart;
        extractTemplateArg(s, valArgEnd); // skip K
        if (valArgEnd < s.size() && s[valArgEnd] == ',') {
            ++valArgEnd;
            while (valArgEnd < s.size() && s[valArgEnd] == ' ') ++valArgEnd;
        }
        auto checkVal = extractTemplateArg(s, valArgEnd); // skip V
        (void)checkVal;

        // valArgEnd now points to ',' before std::less
        s.erase(valArgEnd, trail - 1 - valArgEnd); // keep the final '>'
        pos = valArgEnd + 1;
    }
}

static void collapseDefaultTemplateArgs(std::string& s)
{
    // Simple allocator: vector, list, deque, forward_list
    collapseSimpleAllocator(s, "std::vector");
    collapseSimpleAllocator(s, "std::list");
    collapseSimpleAllocator(s, "std::deque");
    collapseSimpleAllocator(s, "std::forward_list");

    // unique_ptr
    collapseUniquePtr(s);

    // set / multiset
    collapseSet(s, "std::set");
    collapseSet(s, "std::multiset");
    collapseSet(s, "std::unordered_set");
    collapseSet(s, "std::unordered_multiset");

    // map / multimap
    collapseMap(s, "std::map");
    collapseMap(s, "std::multimap");
    collapseMap(s, "std::unordered_map");
    collapseMap(s, "std::unordered_multimap");
}

// ── Pass 6: Normalize whitespace ────────────────────────────────────────────

static void normalizeWhitespace(std::string& s)
{
    // Collapse multiple spaces to one
    size_t pos = 0;
    while ((pos = s.find("  ", pos)) != std::string::npos) {
        size_t end = pos + 2;
        while (end < s.size() && s[end] == ' ') ++end;
        s.erase(pos + 1, end - pos - 1);
    }

    // Remove space before '>'
    replaceAll(s, " >", ">");

    // Remove space after '<'
    replaceAll(s, "< ", "<");

    // Normalize " ," to ","
    replaceAll(s, " ,", ",");

    // Ensure ", " after commas inside template args (but not double-space)
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        if (s[i] == ',' && s[i + 1] != ' ' && s[i + 1] != '>' && s[i + 1] != ')') {
            s.insert(i + 1, 1, ' ');
        }
    }

    // Trim leading/trailing whitespace
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ')  s.pop_back();
}

// ── Public API ──────────────────────────────────────────────────────────────

std::string prettifyCpp(std::string_view raw)
{
    if (raw.empty()) return {};

    std::string s{raw};

    normalizeWhitespace(s);
    replaceLambdaPatterns(s);
    removeCallingConventions(s);
    removeTypeQualifiers(s);
    collapseExactStdTypes(s);
    collapseDefaultTemplateArgs(s);

    return s;
}
