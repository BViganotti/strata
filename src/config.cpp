#include "config.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "util.hpp"

namespace strata {

namespace {

bool globMatchImpl(const char* pattern, const char* text) {
  while (*pattern != '\0') {
    if (*pattern == '*') {
      if (*(pattern + 1) == '*') {
        const char* rest = pattern + 2;
        if (*rest == '/') {
          // `**/` may consume zero directories.
          if (globMatchImpl(rest + 1, text)) return true;
        }
        for (const char* t = text;; ++t) {
          if (globMatchImpl(rest, t)) return true;
          if (*t == '\0') break;
        }
        return false;
      }
      for (const char* t = text;; ++t) {
        if (globMatchImpl(pattern + 1, t)) return true;
        if (*t == '\0' || *t == '/') break;
      }
      return false;
    }
    if (*pattern == '?') {
      if (*text == '\0' || *text == '/') return false;
      ++pattern;
      ++text;
      continue;
    }
    if (*pattern != *text) return false;
    ++pattern;
    ++text;
  }
  return *text == '\0';
}

std::vector<std::string> defaultIgnores() {
  return {"**/vendor/**",     "**/node_modules/**", "**/third_party/**",
          "**/dist/**",       "**/build/**",        "**/target/**",
          "**/.git/**",       "**/.strata/**"};
}

}  // namespace

bool globMatch(const std::string& pattern, const std::string& path) {
  return globMatchImpl(pattern.c_str(), path.c_str());
}

std::string Config::layerFor(const std::string& path) const {
  for (const auto& layer : layers) {
    for (const auto& pattern : layer.paths) {
      // A node is a directory, while patterns usually end in `/**`. Match both
      // the directory itself and anything under it.
      if (globMatch(pattern, path) || globMatch(pattern, path + "/")) {
        return layer.name;
      }
    }
  }
  return "";
}

bool Config::isIgnored(const std::string& path) const {
  for (const auto& pattern : ignore) {
    if (globMatch(pattern, path)) return true;
  }
  return false;
}

bool Config::permits(const std::string& fromLayer, const std::string& toLayer) const {
  if (fromLayer == toLayer) return true;
  auto it = allow.find(fromLayer);
  if (it == allow.end()) return false;
  return it->second.count("*") > 0 || it->second.count(toLayer) > 0;
}

Config loadConfig(const std::string& root) {
  Config config;
  config.ignore = defaultIgnores();

  std::string path = root + "/.strata.json";
  std::ifstream in(path);
  if (!in) return config;

  nlohmann::json parsed;
  try {
    // JSONC: comments are allowed so configs can explain themselves.
    parsed = nlohmann::json::parse(in, nullptr, true, true);
  } catch (const std::exception& error) {
    throw std::runtime_error("cannot parse " + path + ": " + error.what());
  }

  if (parsed.contains("layers")) {
    for (const auto& item : parsed["layers"]) {
      Layer layer;
      layer.name = item.value("name", "");
      if (layer.name.empty()) {
        throw std::runtime_error(path + ": every layer needs a name");
      }
      for (const auto& pattern : item.value("paths", nlohmann::json::array())) {
        layer.paths.push_back(pattern.get<std::string>());
      }
      config.layers.push_back(std::move(layer));
    }
  }

  if (parsed.contains("allow")) {
    for (const auto& [name, deps] : parsed["allow"].items()) {
      auto& set = config.allow[name];
      for (const auto& dep : deps) set.insert(dep.get<std::string>());
    }
  }

  if (parsed.contains("ignore")) {
    for (const auto& pattern : parsed["ignore"]) {
      config.ignore.push_back(pattern.get<std::string>());
    }
  }

  config.maxCycles = parsed.value("max_cycles", -1);
  return config;
}

}  // namespace strata
