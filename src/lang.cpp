#include "lang.hpp"

#include <algorithm>
#include <cctype>

#include "util.hpp"

namespace strata {

namespace {

void addUnique(std::vector<std::string>& out, const std::string& value) {
  if (value.empty()) return;
  if (std::find(out.begin(), out.end(), value) == out.end()) out.push_back(value);
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Returns every double-quoted string in `s`, in order. Go import specs never
// contain escapes, so the simple scan is enough there.
std::vector<std::string> quotedStrings(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] != '"') {
      ++i;
      continue;
    }
    size_t end = i + 1;
    while (end < s.size() && s[end] != '"') ++end;
    if (end >= s.size()) break;
    out.push_back(s.substr(i + 1, end - i - 1));
    i = end + 1;
  }
  return out;
}

std::vector<std::string> goImports(const std::string& src) {
  std::vector<std::string> out;
  bool inBlock = false;
  for (const auto& rawLine : split(src, '\n')) {
    std::string line = trim(rawLine);
    if (inBlock) {
      for (const auto& spec : quotedStrings(line)) addUnique(out, spec);
      if (!line.empty() && line[0] == ')') inBlock = false;
      continue;
    }
    if (!startsWith(line, "import")) continue;
    std::string rest = trim(line.substr(6));
    if (!rest.empty() && rest[0] == '(') {
      inBlock = true;
      rest = rest.substr(1);
    }
    for (const auto& spec : quotedStrings(rest)) addUnique(out, spec);
  }
  return out;
}

// The token immediately before `pos`, skipping whitespace. When it is "(" the
// caller gets "(" so `require(...)` and `import(...)` can be recognised.
std::string previousToken(const std::string& src, size_t pos) {
  size_t i = pos;
  while (i > 0 && std::isspace(static_cast<unsigned char>(src[i - 1]))) --i;
  if (i == 0) return "";
  if (src[i - 1] == '(') return "(";
  size_t end = i;
  while (i > 0) {
    char c = src[i - 1];
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
      --i;
    } else {
      break;
    }
  }
  return src.substr(i, end - i);
}

std::vector<std::string> jsImports(const std::string& src) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < src.size()) {
    char c = src[i];
    if (c != '"' && c != '\'' && c != '`') {
      ++i;
      continue;
    }
    char quote = c;
    size_t end = i + 1;
    while (end < src.size() && src[end] != quote) {
      if (src[end] == '\\') ++end;
      ++end;
    }
    if (end >= src.size()) break;
    std::string value = src.substr(i + 1, end - i - 1);

    std::string token = previousToken(src, i);
    bool isImport = false;
    if (token == "from" || token == "import") {
      isImport = true;
    } else if (token == "(") {
      std::string callee = previousToken(src, i - 1);
      if (callee == "require" || callee == "import") isImport = true;
    }
    if (isImport) addUnique(out, value);
    i = end + 1;
  }
  return out;
}

std::vector<std::string> pythonImports(const std::string& src) {
  std::vector<std::string> out;
  bool continuation = false;
  for (const auto& rawLine : split(src, '\n')) {
    std::string line = trim(rawLine);
    if (continuation) {
      if (!line.empty() && line.back() == '\\') {
        line.pop_back();
        continuation = true;
      } else {
        continuation = false;
      }
    }
    if (startsWith(line, "from ")) {
      size_t importPos = line.find(" import");
      if (importPos != std::string::npos) {
        addUnique(out, trim(line.substr(5, importPos - 5)));
      }
    } else if (startsWith(line, "import ")) {
      std::string rest = line.substr(7);
      size_t comment = rest.find('#');
      if (comment != std::string::npos) rest = rest.substr(0, comment);
      for (const auto& part : split(rest, ',')) {
        std::string name = trim(part);
        size_t space = name.find_first_of(" \t");
        if (space != std::string::npos) name = name.substr(0, space);
        addUnique(out, name);
      }
    }
    if (!line.empty() && line.back() == '\\') continuation = true;
  }
  return out;
}

std::vector<std::string> cppImports(const std::string& src) {
  std::vector<std::string> out;
  for (const auto& rawLine : split(src, '\n')) {
    std::string line = trim(rawLine);
    if (!startsWith(line, "#include")) continue;
    size_t first = line.find('"');
    if (first == std::string::npos) continue;
    size_t second = line.find('"', first + 1);
    if (second == std::string::npos) continue;
    addUnique(out, line.substr(first + 1, second - first - 1));
  }
  return out;
}

std::vector<std::string> rustImports(const std::string& src) {
  std::vector<std::string> out;
  for (const auto& rawLine : split(src, '\n')) {
    std::string line = trim(rawLine);
    if (startsWith(line, "mod ")) {
      std::string rest = trim(line.substr(4));
      size_t semi = rest.find(';');
      if (semi != std::string::npos) addUnique(out, "mod:" + trim(rest.substr(0, semi)));
    } else if (startsWith(line, "pub mod ")) {
      std::string rest = trim(line.substr(8));
      size_t semi = rest.find(';');
      if (semi != std::string::npos) addUnique(out, "mod:" + trim(rest.substr(0, semi)));
    } else if (startsWith(line, "use crate::")) {
      std::string rest = line.substr(11);
      size_t end = rest.find_first_of(";{ ");
      addUnique(out, "crate:" + rest.substr(0, end));
    } else if (startsWith(line, "use self::")) {
      std::string rest = line.substr(10);
      size_t end = rest.find_first_of(";{ ");
      addUnique(out, "self:" + rest.substr(0, end));
    } else if (startsWith(line, "use super::")) {
      std::string rest = line.substr(11);
      size_t end = rest.find_first_of(";{ ");
      addUnique(out, "super:" + rest.substr(0, end));
    }
  }
  return out;
}

std::vector<std::string> javaImports(const std::string& src) {
  std::vector<std::string> out;
  for (const auto& rawLine : split(src, '\n')) {
    std::string line = trim(rawLine);
    if (!startsWith(line, "import ")) continue;
    std::string rest = trim(line.substr(7));
    if (startsWith(rest, "static ")) rest = trim(rest.substr(7));
    size_t semi = rest.find(';');
    if (semi != std::string::npos) rest = rest.substr(0, semi);
    addUnique(out, trim(rest));
  }
  return out;
}

}  // namespace

Lang detectLang(const std::string& path) {
  size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return Lang::Unknown;
  std::string ext = lower(path.substr(dot + 1));
  if (ext == "go") return Lang::Go;
  if (ext == "ts" || ext == "tsx" || ext == "js" || ext == "jsx" ||
      ext == "mjs" || ext == "cjs"|| ext == "mts" || ext == "cts") {
    return Lang::TypeScript;
  }
  if (ext == "py") return Lang::Python;
  if (ext == "c" || ext == "cc" || ext == "cpp" || ext == "cxx" ||
      ext == "h" || ext == "hh" || ext == "hpp" || ext == "hxx") {
    return Lang::Cpp;
  }
  if (ext == "rs") return Lang::Rust;
  if (ext == "java" || ext == "kt" || ext == "kts") return Lang::Java;
  return Lang::Unknown;
}

std::string langName(Lang lang) {
  switch (lang) {
    case Lang::Go:
      return "go";
    case Lang::TypeScript:
      return "ts";
    case Lang::Python:
      return "python";
    case Lang::Cpp:
      return "cpp";
    case Lang::Rust:
      return "rust";
    case Lang::Java:
      return "java";
    case Lang::Unknown:
      return "";
  }
  return "";
}

std::vector<std::string> extractImports(Lang lang, const std::string& source) {
  switch (lang) {
    case Lang::Go:
      return goImports(source);
    case Lang::TypeScript:
      return jsImports(source);
    case Lang::Python:
      return pythonImports(source);
    case Lang::Cpp:
      return cppImports(source);
    case Lang::Rust:
      return rustImports(source);
    case Lang::Java:
      return javaImports(source);
    case Lang::Unknown:
      return {};
  }
  return {};
}

}  // namespace strata
