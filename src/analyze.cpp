#include "analyze.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "config.hpp"
#include "lang.hpp"
#include "util.hpp"

namespace strata {

namespace {

struct SourceFile {
  std::string path;
  Lang lang;
  int loc = 0;
  std::vector<std::string> imports;
};

int countLines(const std::string& source) {
  if (source.empty()) return 0;
  int lines = 0;
  for (char c : source) {
    if (c == '\n') ++lines;
  }
  if (source.back() != '\n') ++lines;
  return lines;
}

std::string parseGoModule(const std::string& gomod) {
  for (const auto& rawLine : split(gomod, '\n')) {
    std::string line = trim(rawLine);
    if (!startsWith(line, "module ")) continue;
    std::string name = trim(line.substr(7));
    if (!name.empty() && name.front() == '"' && name.back() == '"') {
      name = name.substr(1, name.size() - 2);
    }
    return name;
  }
  return "";
}

std::string replaceDots(const std::string& value) {
  std::string out = value;
  for (char& c : out) {
    if (c == '.') c = '/';
  }
  return out;
}

bool hasFile(const std::set<std::string>& files, const std::string& path) {
  return files.count(path) > 0;
}

// Resolves an import specifier to a directory that exists in this snapshot.
std::string resolveImport(
    Lang lang, const std::string& file, const std::string& spec,
    const std::set<std::string>& files, const std::set<std::string>& dirs,
    const std::string& goModule,
    const std::map<std::string, std::vector<std::string>>& byName) {
  if (spec.empty()) return "";
  std::string fileDir = dirName(file);

  switch (lang) {
    case Lang::Go: {
      if (goModule.empty()) {
        if (dirs.count(normalizePath(spec))) return normalizePath(spec);
        return "";
      }
      if (spec == goModule) return ".";
      const std::string prefix = goModule + "/";
      if (startsWith(spec, prefix)) {
        std::string rel = normalizePath(spec.substr(prefix.size()));
        if (dirs.count(rel)) return rel;
      }
      return "";
    }

    case Lang::TypeScript: {
      if (spec.front() != '.') return "";
      std::string base = joinPath(fileDir, spec);
      static const char* kExtensions[] = {"",     ".ts",  ".tsx",  ".js",
                                          ".jsx", ".mjs", ".cjs",  ".json"};
      for (const char* extension : kExtensions) {
        if (hasFile(files, base + extension)) return dirName(base + extension);
      }
      for (const char* extension : kExtensions) {
        std::string index = base + "/index" + extension;
        if (hasFile(files, index)) return base;
      }
      if (dirs.count(base)) return base;
      return "";
    }

    case Lang::Python: {
      if (spec.front() == '.') {
        size_t dots = 0;
        while (dots < spec.size() && spec[dots] == '.') ++dots;
        std::string base = fileDir;
        for (size_t i = 1; i < dots; ++i) base = dirName(base);
        std::string rest = spec.substr(dots);
        if (rest.empty()) return dirs.count(base) ? base : "";
        std::string target = joinPath(base, replaceDots(rest));
        if (hasFile(files, target + ".py")) return dirName(target + ".py");
        if (hasFile(files, target + "/__init__.py")) return target;
        if (dirs.count(target)) return target;
        return "";
      }
      std::string target = replaceDots(spec);
      if (hasFile(files, target + ".py")) return dirName(target + ".py");
      if (hasFile(files, target + "/__init__.py")) return target;
      if (dirs.count(target)) return target;
      return "";
    }

    case Lang::Cpp: {
      std::string local = joinPath(fileDir, spec);
      if (hasFile(files, local)) return dirName(local);
      std::string rooted = normalizePath(spec);
      if (hasFile(files, rooted)) return dirName(rooted);
      if (dirs.count(rooted)) return rooted;

      // C and C++ projects commonly rely on include directories. Rather than
      // parse a build system, accept a unique path suffix: `"core.hpp"` can
      // mean `src/core.hpp`. Ambiguity is resolved toward the including file.
      auto it = byName.find(baseName(spec));
      if (it == byName.end()) return "";
      const std::string suffix = "/" + spec;
      const std::string* best = nullptr;
      int bestScore = -1;
      for (const auto& candidate : it->second) {
        if (candidate != spec && !endsWith(candidate, suffix)) continue;
        std::vector<std::string> left = split(fileDir, '/');
        std::vector<std::string> right = split(dirName(candidate), '/');
        int score = 0;
        while (score < static_cast<int>(left.size()) &&
               score < static_cast<int>(right.size()) &&
               left[score] == right[score]) {
          ++score;
        }
        if (score > bestScore) {
          bestScore = score;
          best = &candidate;
        }
      }
      return best ? dirName(*best) : "";
    }

    case Lang::Rust: {
      if (startsWith(spec, "mod:")) {
        std::string name = spec.substr(4);
        std::string sibling = joinPath(fileDir, name);
        if (hasFile(files, sibling + ".rs")) return fileDir;
        if (hasFile(files, sibling + "/mod.rs")) return sibling;
        return "";
      }
      std::string path = spec;
      std::string base = ".";
      if (startsWith(path, "crate:")) {
        path = path.substr(6);
      } else if (startsWith(path, "self:")) {
        path = path.substr(5);
        base = fileDir;
      } else if (startsWith(path, "super:")) {
        path = path.substr(6);
        base = dirName(fileDir);
      } else {
        return "";
      }
      std::string target = joinPath(base, replaceDots(path));
      if (hasFile(files, target + ".rs")) return dirName(target + ".rs");
      if (hasFile(files, target + "/mod.rs")) return target;
      if (dirs.count(target)) return target;
      return "";
    }

    case Lang::Java: {
      std::string path = replaceDots(spec);
      for (const char* extension : {".java", ".kt", ".kts"}) {
        if (hasFile(files, path + extension)) return dirName(path + extension);
      }
      if (dirs.count(path)) return path;
      std::string parent = dirName(path);
      if (dirs.count(parent)) return parent;
      return "";
    }

    case Lang::Unknown:
      return "";
  }
  return "";
}

}  // namespace

Graph buildGraph(const Git& git, const std::string& sha,
                 const std::vector<std::string>& ignore,
                 const std::map<std::string, int>& churn) {
  Graph graph;

  std::vector<TreeEntry> entries = git.lsTree(sha);
  std::set<std::string> files;
  std::set<std::string> dirs;
  std::string goModule;

  Config filter;
  filter.ignore = ignore;

  for (const auto& entry : entries) {
    if (filter.isIgnored(entry.path)) continue;
    files.insert(entry.path);
    dirs.insert(dirName(entry.path));
    if (entry.path == "go.mod") {
      goModule = parseGoModule(git.readBlob(entry.blob));
    }
  }

  std::map<std::string, std::map<Lang, int>> languageLoc;
  std::map<std::string, std::vector<std::string>> byName;
  std::vector<SourceFile> sources;

  for (const auto& path : files) byName[baseName(path)].push_back(path);

  for (const auto& entry : entries) {
    Lang lang = detectLang(entry.path);
    if (lang == Lang::Unknown) continue;
    if (!files.count(entry.path)) continue;

    SourceFile source;
    source.path = entry.path;
    source.lang = lang;
    std::string content = git.readBlob(entry.blob);
    source.loc = countLines(content);
    source.imports = extractImports(lang, content);

    Node& node = graph.addNode(dirName(entry.path));
    node.loc += source.loc;
    node.files += 1;
    languageLoc[node.path][lang] += source.loc;
    sources.push_back(std::move(source));
  }

  for (auto& [directory, node] : graph.nodes) {
    auto it = churn.find(directory);
    node.churn = it == churn.end() ? 0 : it->second;

    int best = -1;
    for (const auto& [lang, loc] : languageLoc[directory]) {
      if (loc > best) {
        best = loc;
        node.lang = langName(lang);
      }
    }
  }

  for (const auto& source : sources) {
    const std::string from = dirName(source.path);
    for (const auto& spec : source.imports) {
      std::string target = resolveImport(source.lang, source.path, spec, files, dirs,
                                         goModule, byName);
      if (target.empty() || target == from) continue;
      graph.addEdge(from, target, 1);
    }
  }

  graph.computeCycles();
  return graph;
}

}  // namespace strata
