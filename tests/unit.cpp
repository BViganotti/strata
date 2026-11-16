#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "check.hpp"
#include "config.hpp"
#include "graph.hpp"
#include "lang.hpp"
#include "util.hpp"

using namespace strata;

namespace {

int failures = 0;

template <typename A, typename B>
void checkEqual(const A& a, const B& b, const char* expression, const char* file,
                int line) {
  if (!(a == b)) {
    std::cerr << file << ":" << line << " CHECK_EQ failed: " << expression << "\n";
    ++failures;
  }
}

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #condition \
                << "\n";                                                     \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

#define CHECK_EQ(a, b) checkEqual((a), (b), #a " == " #b, __FILE__, __LINE__)

void testGlob() {
  CHECK(globMatch("**/third_party/**", "third_party/httplib/httplib.h"));
  CHECK(globMatch("**/vendor/**", "a/vendor/b.go"));
  CHECK(globMatch("vendor/**", "vendor/b.go"));
  CHECK(globMatch("src/*.cpp", "src/a.cpp"));
  CHECK(!globMatch("src/*.cpp", "src/a/b.cpp"));
  CHECK(globMatch("src/**", "src/a/b.cpp"));
  CHECK(globMatch("**/*_test.go", "pkg/x_test.go"));
  CHECK(globMatch("**/*_test.go", "x_test.go"));
  CHECK(globMatch("src/?ood", "src/food"));
  CHECK(!globMatch("src/?ood", "src/foood"));
  CHECK(globMatch("a/**/z", "a/z"));
  CHECK(globMatch("a/**/z", "a/b/c/z"));
  CHECK(globMatch("**/build/**", "build/x"));
}

void testLanguageDetection() {
  CHECK(detectLang("src/main.go") == Lang::Go);
  CHECK(detectLang("web/app.tsx") == Lang::TypeScript);
  CHECK(detectLang("web/app.mjs") == Lang::TypeScript);
  CHECK(detectLang("tools/run.py") == Lang::Python);
  CHECK(detectLang("cpp/src/server.hpp") == Lang::Cpp);
  CHECK(detectLang("src/lib.rs") == Lang::Rust);
  CHECK(detectLang("src/Main.java") == Lang::Java);
  CHECK(detectLang("README.md") == Lang::Unknown);
  CHECK_EQ(langName(Lang::Cpp), std::string("cpp"));
}

void testGoImports() {
  const std::string source =
      "package main\n"
      "import (\n"
      "  \"fmt\"\n"
      "  store \"example.com/redlite/internal/store\"\n"
      ")\n"
      "import \"os\"\n";
  auto imports = extractImports(Lang::Go, source);
  CHECK_EQ(imports.size(), size_t(3));
  CHECK_EQ(imports[0], std::string("fmt"));
  CHECK_EQ(imports[1], std::string("example.com/redlite/internal/store"));
  CHECK_EQ(imports[2], std::string("os"));
}

void testJsImports() {
  const std::string source =
      "import { a } from \"./a\";\n"
      "const b = require('../b');\n"
      "import(\"./c\");\n"
      "export * from \"./d\";\n"
      "const notAnImport = \"hello\";\n";
  auto imports = extractImports(Lang::TypeScript, source);
  CHECK_EQ(imports.size(), size_t(4));
  CHECK_EQ(imports[0], std::string("./a"));
  CHECK_EQ(imports[1], std::string("../b"));
  CHECK_EQ(imports[2], std::string("./c"));
  CHECK_EQ(imports[3], std::string("./d"));
}

void testPythonImports() {
  const std::string source =
      "from .core import thing\n"
      "import os, pkg.mod\n"
      "from collections import OrderedDict\n";
  auto imports = extractImports(Lang::Python, source);
  CHECK_EQ(imports.size(), size_t(4));
  CHECK_EQ(imports[0], std::string(".core"));
  CHECK_EQ(imports[1], std::string("os"));
  CHECK_EQ(imports[2], std::string("pkg.mod"));
  CHECK_EQ(imports[3], std::string("collections"));
}

void testCppImports() {
  const std::string source =
      "#include \"resp.hpp\"\n"
      "#include <vector>\n"
      "  #include \"store.hpp\"\n";
  auto imports = extractImports(Lang::Cpp, source);
  CHECK_EQ(imports.size(), size_t(2));
  CHECK_EQ(imports[0], std::string("resp.hpp"));
  CHECK_EQ(imports[1], std::string("store.hpp"));
}

void testRustImports() {
  const std::string source =
      "mod server;\n"
      "pub mod handlers;\n"
      "use crate::store::db;\n"
      "use std::collections::HashMap;\n";
  auto imports = extractImports(Lang::Rust, source);
  CHECK_EQ(imports.size(), size_t(3));
  CHECK_EQ(imports[0], std::string("mod:server"));
  CHECK_EQ(imports[1], std::string("mod:handlers"));
  CHECK_EQ(imports[2], std::string("crate:store::db"));
}

void testJavaImports() {
  const std::string source =
      "import static a.b.C.d;\n"
      "import x.y.Z;\n";
  auto imports = extractImports(Lang::Java, source);
  CHECK_EQ(imports.size(), size_t(2));
  CHECK_EQ(imports[0], std::string("a.b.C.d"));
  CHECK_EQ(imports[1], std::string("x.y.Z"));
}

void testCycles() {
  Graph graph;
  for (const char* path : {"a", "b", "c", "d"}) graph.addNode(path);
  graph.addEdge("a", "b", 1);
  graph.addEdge("b", "c", 1);
  graph.addEdge("c", "a", 1);
  graph.addEdge("c", "d", 1);
  graph.computeCycles();

  CHECK_EQ(graph.cycles.size(), size_t(1));
  CHECK_EQ(graph.cycles[0].size(), size_t(3));
  auto nodes = graph.cycleNodes();
  CHECK_EQ(nodes.size(), size_t(3));
  CHECK(nodes.count("a"));
  CHECK(nodes.count("b"));
  CHECK(nodes.count("c"));
  CHECK(!nodes.count("d"));
  CHECK_EQ(graph.cycleEdges().size(), size_t(3));

  Graph acyclic;
  acyclic.addNode("x");
  acyclic.addNode("y");
  acyclic.addEdge("x", "y", 1);
  acyclic.computeCycles();
  CHECK(acyclic.cycles.empty());
}

void testGraphDiff() {
  Graph before;
  before.addNode("a");
  before.addNode("b");
  before.addEdge("a", "b", 1);

  Graph after = before;
  after.addNode("c");
  after.addEdge("b", "c", 2);
  after.nodes["a"].loc = 120;

  GraphDiff diff = diffGraphs(before, after);
  CHECK_EQ(diff.addedNodes.size(), size_t(1));
  CHECK_EQ(diff.addedNodes[0], std::string("c"));
  CHECK(diff.removedNodes.empty());
  CHECK_EQ(diff.addedEdges.size(), size_t(1));
  CHECK_EQ(diff.changedNodes.size(), size_t(1));
  CHECK_EQ(diff.changedNodes[0].locAfter, 120);
}

void testPaths() {
  CHECK_EQ(normalizePath("./a/b"), std::string("a/b"));
  CHECK_EQ(normalizePath("a/../b"), std::string("b"));
  CHECK_EQ(dirName("src/main.cpp"), std::string("src"));
  CHECK_EQ(dirName("main.cpp"), std::string("."));
  CHECK_EQ(baseName("src/main.cpp"), std::string("main.cpp"));
  CHECK_EQ(baseName("."), std::string("."));
  CHECK_EQ(joinPath("src", "../lib/x"), std::string("lib/x"));
  CHECK_EQ(joinPath(".", "a"), std::string("a"));
}

void testConfig() {
  auto directory = std::filesystem::temp_directory_path() /
                   ("strata-test-" + std::to_string(std::rand()));
  std::filesystem::create_directories(directory);
  std::string path = (directory / ".strata.json").string();

  {
    std::ofstream out(path);
    out << R"({
      // comments are allowed
      "layers": [
        {"name": "core", "paths": ["core/**"]},
        {"name": "api", "paths": ["api/**"]}
      ],
      "allow": {"core": [], "api": ["core"]},
      "ignore": ["**/generated/**"],
      "max_cycles": 0
    })";
  }

  Config config = loadConfig(directory.string());
  CHECK_EQ(config.layers.size(), size_t(2));
  CHECK_EQ(config.layerFor("core/store.cpp"), std::string("core"));
  CHECK_EQ(config.layerFor("api/server.cpp"), std::string("api"));
  CHECK_EQ(config.layerFor("core"), std::string("core"));
  CHECK_EQ(config.layerFor("api"), std::string("api"));
  CHECK_EQ(config.layerFor("docs/readme.md"), std::string(""));
  CHECK(config.permits("api", "core"));
  CHECK(!config.permits("core", "api"));
  CHECK(config.isIgnored("pkg/generated/file.go"));
  CHECK_EQ(config.maxCycles, 0);

  Graph graph;
  graph.addNode("core/a");
  graph.addNode("api/b");
  graph.addEdge("core/a", "api/b", 1);
  auto violations = findViolations(graph, config);
  CHECK_EQ(violations.size(), size_t(1));
  CHECK_EQ(violations[0].kind, std::string("layer"));

  auto fresh = newViolations(violations, violations);
  CHECK(fresh.empty());

  std::filesystem::remove_all(directory);
}

void testViolations() {
  Config config;
  config.layers = {{"domain", {"domain/**"}}, {"infra", {"infra/**"}}};
  config.allow["domain"] = {};
  config.allow["infra"] = {"domain"};

  Graph graph;
  graph.addNode("domain/a");
  graph.addNode("infra/b");
  graph.addEdge("domain/a", "infra/b", 1);
  auto violations = findViolations(graph, config);
  CHECK_EQ(violations.size(), size_t(1));

  Graph after;
  after.addNode("domain/x");
  after.addNode("domain/y");
  after.addEdge("domain/x", "domain/y", 1);
  after.addEdge("domain/y", "domain/x", 1);
  after.computeCycles();
  auto cycleViolations = findViolations(after, config);
  CHECK_EQ(cycleViolations.size(), size_t(1));
  CHECK_EQ(cycleViolations[0].kind, std::string("cycle"));
}

}  // namespace

int main() {
  testGlob();
  testLanguageDetection();
  testGoImports();
  testJsImports();
  testPythonImports();
  testCppImports();
  testRustImports();
  testJavaImports();
  testCycles();
  testGraphDiff();
  testPaths();
  testConfig();
  testViolations();

  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "all tests passed\n";
  return 0;
}
