#pragma once
#include <string>
#include <string_view>

// Apply C++ prettification rules to a symbol name.
// Rules: lambda cleanup, calling convention removal, class/struct/enum qualifier removal,
// std:: type collapsing, default template arg collapsing, whitespace normalization.
std::string prettifyCpp(std::string_view raw);
