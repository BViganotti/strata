#pragma once

#include <string>
#include <vector>

namespace strata {

enum class Lang { Unknown, Go, TypeScript, Python, Cpp, Rust, Java };

Lang detectLang(const std::string& path);
std::string langName(Lang lang);

// Returns the module specifiers found in one source file. This is deliberately
// a lexical pass: it never executes code and never needs a compiler.
std::vector<std::string> extractImports(Lang lang, const std::string& source);

}  // namespace strata
