#include "PrettifyRust.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

// ── Helpers ──────────────────────────────────────────────────────────────────

// Check if position is at a Rust path boundary (start of string or after `::`).
static bool atPathBoundary(const std::string& s, size_t pos)
{
    if (pos == 0) return true;
    if (pos >= 2 && s[pos - 1] == ':' && s[pos - 2] == ':') return true;
    // Also after '<', ',', '(', or space (template/parameter context)
    char prev = s[pos - 1];
    return prev == '<' || prev == ',' || prev == '(' || prev == ' ';
}

// ── Pass 1: Shorten alloc:: paths ───────────────────────────────────────────

static void shortenAllocPaths(std::string& s)
{
    // Replace well-known alloc:: re-exports with their short names.
    // Only replace when at a path boundary to avoid false matches.
    struct Replacement {
        std::string_view from;
        std::string_view to;
    };

    static constexpr Replacement replacements[] = {
        {"alloc::string::String",        "String"},
        {"alloc::vec::Vec",              "Vec"},
        {"alloc::boxed::Box",            "Box"},
        {"alloc::rc::Rc",                "Rc"},
        {"alloc::sync::Arc",             "Arc"},
        {"alloc::collections::BTreeMap", "BTreeMap"},
        {"alloc::collections::BTreeSet", "BTreeSet"},
        {"alloc::collections::VecDeque", "VecDeque"},
        {"alloc::collections::BinaryHeap", "BinaryHeap"},
        {"alloc::collections::LinkedList", "LinkedList"},
        {"alloc::borrow::Cow",           "Cow"},
        {"alloc::fmt::format",           "format"},
    };

    for (auto& [from, to] : replacements) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            if (atPathBoundary(s, pos)) {
                s.replace(pos, from.size(), to);
                pos += to.size();
            } else {
                pos += from.size();
            }
        }
    }
}

// ── Pass 2: Shorten core:: paths ────────────────────────────────────────────

static void shortenCorePaths(std::string& s)
{
    struct Replacement {
        std::string_view from;
        std::string_view to;
    };

    static constexpr Replacement replacements[] = {
        {"core::option::Option",       "Option"},
        {"core::result::Result",       "Result"},
        {"core::fmt::Display",         "Display"},
        {"core::fmt::Debug",           "Debug"},
        {"core::fmt::Formatter",       "Formatter"},
        {"core::iter::Iterator",       "Iterator"},
        {"core::convert::From",        "From"},
        {"core::convert::Into",        "Into"},
        {"core::convert::TryFrom",     "TryFrom"},
        {"core::convert::TryInto",     "TryInto"},
        {"core::clone::Clone",         "Clone"},
        {"core::cmp::PartialEq",       "PartialEq"},
        {"core::cmp::Eq",              "Eq"},
        {"core::cmp::PartialOrd",      "PartialOrd"},
        {"core::cmp::Ord",             "Ord"},
        {"core::cmp::Ordering",        "Ordering"},
        {"core::hash::Hash",           "Hash"},
        {"core::hash::Hasher",         "Hasher"},
        {"core::marker::Send",         "Send"},
        {"core::marker::Sync",         "Sync"},
        {"core::marker::Sized",        "Sized"},
        {"core::marker::Copy",         "Copy"},
        {"core::marker::PhantomData",  "PhantomData"},
        {"core::ops::Drop",            "Drop"},
        {"core::ops::Fn",              "Fn"},
        {"core::ops::FnMut",           "FnMut"},
        {"core::ops::FnOnce",          "FnOnce"},
        {"core::ops::Deref",           "Deref"},
        {"core::ops::DerefMut",        "DerefMut"},
        {"core::ops::Index",           "Index"},
        {"core::ops::IndexMut",        "IndexMut"},
        {"core::default::Default",     "Default"},
        {"core::future::Future",       "Future"},
        {"core::pin::Pin",             "Pin"},
        {"core::task::Poll",           "Poll"},
        {"core::task::Context",        "Context"},
        {"core::cell::Cell",           "Cell"},
        {"core::cell::RefCell",        "RefCell"},
        {"core::mem::MaybeUninit",     "MaybeUninit"},
        {"core::num::NonZeroU8",       "NonZeroU8"},
        {"core::num::NonZeroU16",      "NonZeroU16"},
        {"core::num::NonZeroU32",      "NonZeroU32"},
        {"core::num::NonZeroU64",      "NonZeroU64"},
        {"core::num::NonZeroUsize",    "NonZeroUsize"},
    };

    for (auto& [from, to] : replacements) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            if (atPathBoundary(s, pos)) {
                s.replace(pos, from.size(), to);
                pos += to.size();
            } else {
                pos += from.size();
            }
        }
    }
}

// ── Pass 3: Clean closure$/impl$ artifacts ──────────────────────────────────

static void cleanArtifacts(std::string& s)
{
    // Replace closure$N with <closure>
    {
        constexpr std::string_view prefix = "closure$";
        size_t pos = 0;
        while ((pos = s.find(prefix, pos)) != std::string::npos) {
            size_t numStart = pos + prefix.size();
            size_t numEnd = numStart;
            while (numEnd < s.size() && std::isdigit(static_cast<unsigned char>(s[numEnd])))
                ++numEnd;
            if (numEnd > numStart) {
                s.replace(pos, numEnd - pos, "<closure>");
                pos += 9; // length of "<closure>"
            } else {
                pos = numStart;
            }
        }
    }

    // Replace impl$N with impl (keep the word, strip the numeric suffix)
    {
        constexpr std::string_view prefix = "impl$";
        size_t pos = 0;
        while ((pos = s.find(prefix, pos)) != std::string::npos) {
            size_t numStart = pos + prefix.size();
            size_t numEnd = numStart;
            while (numEnd < s.size() && std::isdigit(static_cast<unsigned char>(s[numEnd])))
                ++numEnd;
            if (numEnd > numStart) {
                s.replace(pos, numEnd - pos, "impl");
                pos += 4; // length of "impl"
            } else {
                pos = numStart;
            }
        }
    }
}

// ── Pass 4: Shorten std:: re-exports (Rust std) ─────────────────────────────

static void shortenRustStdPaths(std::string& s)
{
    // Rust's std:: re-exports from alloc/core. These patterns specifically
    // target Rust-style paths (e.g., std::collections::HashMap) which do NOT
    // conflict with C++ std:: patterns (e.g., std::unordered_map<>).
    struct Replacement {
        std::string_view from;
        std::string_view to;
    };

    static constexpr Replacement replacements[] = {
        {"std::collections::HashMap",     "HashMap"},
        {"std::collections::HashSet",     "HashSet"},
        {"std::collections::BTreeMap",    "BTreeMap"},
        {"std::collections::BTreeSet",    "BTreeSet"},
        {"std::collections::VecDeque",    "VecDeque"},
        {"std::collections::BinaryHeap",  "BinaryHeap"},
        {"std::collections::LinkedList",  "LinkedList"},
        {"std::sync::Mutex",              "Mutex"},
        {"std::sync::RwLock",             "RwLock"},
        {"std::sync::mpsc::Sender",       "Sender"},
        {"std::sync::mpsc::Receiver",     "Receiver"},
        {"std::io::Read",                 "io::Read"},
        {"std::io::Write",                "io::Write"},
        {"std::io::Error",                "io::Error"},
        {"std::io::Result",               "io::Result"},
        {"std::path::Path",               "Path"},
        {"std::path::PathBuf",            "PathBuf"},
        {"std::ffi::OsStr",               "OsStr"},
        {"std::ffi::OsString",            "OsString"},
        {"std::ffi::CStr",                "CStr"},
        {"std::ffi::CString",             "CString"},
    };

    for (auto& [from, to] : replacements) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            if (atPathBoundary(s, pos)) {
                s.replace(pos, from.size(), to);
                pos += to.size();
            } else {
                pos += from.size();
            }
        }
    }
}

// ── Public API ──────────────────────────────────────────────────────────────

std::string prettifyRust(std::string_view raw)
{
    if (raw.empty()) return {};

    std::string s{raw};

    shortenAllocPaths(s);
    shortenCorePaths(s);
    cleanArtifacts(s);
    shortenRustStdPaths(s);

    return s;
}
