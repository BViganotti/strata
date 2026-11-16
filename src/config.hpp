#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace strata {

struct Layer {
  std::string name;
  std::vector<std::string> paths;
};

struct Config {
  std::vector<Layer> layers;
  // allow["domain"] == {"infra"} means a module in `domain` may depend on
  // `infra`. "*" allows every layer.
  std::map<std::string, std::set<std::string>> allow;
  std::vector<std::string> ignore;
  // -1 disables the absolute gate; the ratchet still flags new cycles.
  int maxCycles = -1;

  std::string layerFor(const std::string& path) const;
  bool isIgnored(const std::string& path) const;
  bool permits(const std::string& fromLayer, const std::string& toLayer) const;
};

// Reads <root>/.strata.json when present. Missing file means defaults.
Config loadConfig(const std::string& root);

// Glob with `*`, `?`, and `**`. `**` crosses directory separators.
bool globMatch(const std::string& pattern, const std::string& path);

}  // namespace strata
