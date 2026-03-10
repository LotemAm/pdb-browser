#pragma once
#include <string>
#include <string_view>

// Apply Rust prettification rules to a symbol name.
// Rules: shorten alloc::/core::/std:: paths, clean closure$/impl$ artifacts.
std::string prettifyRust(std::string_view raw);
