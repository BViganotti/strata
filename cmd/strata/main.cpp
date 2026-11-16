#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "analyze.hpp"
#include "check.hpp"
#include "config.hpp"
#include "git.hpp"
#include "graph.hpp"
#include "store.hpp"
#include "util.hpp"
#include "web.hpp"

#include <nlohmann/json.hpp>

#ifndef STRATA_VERSION
#define STRATA_VERSION "0.1.0"
#endif

namespace {

using namespace strata;

struct Args {
  std::map<std::string, std::string> flags;
  std::vector<std::string> positional;

  bool has(const std::string& key) const { return flags.count(key) > 0; }

  std::string get(const std::string& key, const std::string& fallback = "") const {
    auto it = flags.find(key);
    return it == flags.end() ? fallback : it->second;
  }

  int getInt(const std::string& key, int fallback) const {
    auto it = flags.find(key);
    if (it == flags.end()) return fallback;
    return std::stoi(it->second);
  }
};

Args parseArgs(int argc, char** argv, int start) {
  Args args;
  for (int i = start; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      args.flags["help"] = "true";
      continue;
    }
    if (!startsWith(arg, "--")) {
      args.positional.push_back(arg);
      continue;
    }
    std::string key = arg.substr(2);
    std::string value = "true";
    size_t equals = key.find('=');
    if (equals != std::string::npos) {
      value = key.substr(equals + 1);
      key = key.substr(0, equals);
    } else if (i + 1 < argc && !startsWith(argv[i + 1], "--")) {
      value = argv[++i];
    }
    args.flags[key] = value;
  }
  return args;
}

void usage() {
  std::cout <<
      "strata " STRATA_VERSION " - the architecture time machine\n"
      "\n"
      "Usage:\n"
      "  strata ingest [path] [--db strata.db] [--limit N] [--stride N]\n"
      "                       [--since DATE] [--name NAME]\n"
      "  strata serve  [--db strata.db] [--addr 127.0.0.1:7777] [--web-dir DIR]\n"
      "  strata check  [--repo PATH] --baseline REV [--target REV]\n"
      "                       [--format text|json|markdown]\n"
      "  strata diff   FROM TO [--repo PATH] [--format text|json]\n"
      "  strata version\n"
      "\n"
      "Examples:\n"
      "  strata ingest . --limit 200\n"
      "  strata serve\n"
      "  strata check --baseline origin/main\n"
      "  strata diff v1.0.0 HEAD\n";
}

std::string shortSha(const std::string& sha) {
  return sha.size() > 7 ? sha.substr(0, 7) : sha;
}

std::string parentsOf(const Commit& commit) {
  std::string parents;
  for (size_t i = 0; i < commit.parents.size(); ++i) {
    if (i) parents += " ";
    parents += commit.parents[i];
  }
  return parents;
}

void printViolations(const std::vector<Violation>& violations) {
  for (const auto& violation : violations) {
    std::cout << "  x " << violation.detail << "\n";
  }
}

int commandIngest(const Args& args) {
  std::string path = args.positional.empty() ? "." : args.positional[0];
  std::string db = args.get("db", "strata.db");
  int limit = args.getInt("limit", 0);
  int stride = args.getInt("stride", 1);
  std::string since = args.get("since");

  if (limit < 0) throw std::runtime_error("--limit must be >= 0");
  if (stride < 1) throw std::runtime_error("--stride must be >= 1");

  Git git(path);
  Config config = loadConfig(git.root());
  std::string name = args.get("name", git.remoteName());

  auto started = std::chrono::steady_clock::now();
  std::vector<Commit> commits = git.log(limit, stride, since);
  if (commits.empty()) throw std::runtime_error("no commits found in " + git.root());

  Store store(db);
  store.init();
  int repoId = store.upsertRepo(git.root(), name, git.head());
  store.clearSnapshots(repoId);

  std::map<std::string, int> churn;
  Graph last;
  int stored = 0;

  for (size_t i = 0; i < commits.size(); ++i) {
    const Commit& commit = commits[i];
    for (const auto& changed : git.changedFiles(commit.sha)) {
      if (config.isIgnored(changed)) continue;
      churn[dirName(changed)] += 1;
    }

    Graph graph = buildGraph(git, commit.sha, config.ignore, churn);
    store.addSnapshot(repoId, static_cast<int>(i), commit.sha, commit.time,
                      commit.subject, parentsOf(commit), graph);
    last = std::move(graph);
    ++stored;

    int percent = static_cast<int>((100 * (i + 1)) / commits.size());
    std::cerr << "\r  parsing " << (i + 1) << "/" << commits.size() << "  "
              << percent << "%";
    std::cerr.flush();
  }
  std::cerr << "\n";

  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - started)
                     .count();

  std::cout << "ingested " << name << "\n";
  std::cout << "  repo      " << git.root() << "\n";
  std::cout << "  db        " << db << "\n";
  std::cout << "  snapshots " << stored << "\n";
  std::cout << "  head      " << shortSha(commits.back().sha) << " "
            << commits.back().subject << "\n";
  std::cout << "  modules   " << last.nodes.size() << "\n";
  std::cout << "  deps      " << last.edges.size() << "\n";
  std::cout << "  cycles    " << last.cycles.size() << "\n";
  std::cout << "  loc       " << last.totalLoc() << "\n";
  std::cout << "  elapsed   " << elapsed << " ms\n";
  std::cout << "\nnext: strata serve --db " << db << "\n";
  return 0;
}

int commandServe(const Args& args) {
  std::string db = args.get("db", "strata.db");
  std::string addr = args.get("addr", "127.0.0.1:7777");
  std::string webDir = args.get("web-dir");
  serve(db, addr, webDir);
  return 0;
}

int commandCheck(const Args& args) {
  std::string path = args.get("repo", ".");
  std::string baseline = args.get("baseline");
  std::string target = args.get("target", "HEAD");
  std::string format = args.get("format", "text");
  if (baseline.empty()) {
    throw std::runtime_error("check needs --baseline <rev>, for example origin/main");
  }

  Git git(path);
  Config config = loadConfig(git.root());
  std::string baselineSha = git.resolve(baseline);
  std::string targetSha = git.resolve(target);

  std::map<std::string, int> churn;
  Graph before = buildGraph(git, baselineSha, config.ignore, churn);
  Graph after = buildGraph(git, targetSha, config.ignore, churn);

  std::vector<Violation> baselineViolations = findViolations(before, config);
  std::vector<Violation> currentViolations = findViolations(after, config);
  std::vector<Violation> fresh = newViolations(baselineViolations, currentViolations);
  bool overLimit =
      config.maxCycles >= 0 && static_cast<int>(after.cycles.size()) > config.maxCycles;

  if (format == "json") {
    nlohmann::json out;
    out["baseline"] = {{"rev", baseline},
                       {"sha", baselineSha},
                       {"cycles", before.cycles.size()},
                       {"violations", baselineViolations.size()}};
    out["target"] = {{"rev", target},
                     {"sha", targetSha},
                     {"cycles", after.cycles.size()},
                     {"violations", currentViolations.size()}};
    out["new"] = nlohmann::json::array();
    for (const auto& violation : fresh) {
      out["new"].push_back({{"kind", violation.kind},
                            {"key", violation.key},
                            {"detail", violation.detail}});
    }
    out["maxCycles"] = config.maxCycles;
    out["overLimit"] = overLimit;
    std::cout << out.dump(2) << "\n";
  } else if (format == "markdown") {
    std::cout << "## strata check\n\n";
    std::cout << "| | baseline | target |\n|---|---|---|\n";
    std::cout << "| cycles | " << before.cycles.size() << " | " << after.cycles.size()
              << " |\n";
    std::cout << "| violations | " << baselineViolations.size() << " | "
              << currentViolations.size() << " |\n\n";
    if (fresh.empty() && !overLimit) {
      std::cout << "No new architectural regressions.\n";
    } else {
      std::cout << "### New regressions\n\n";
      for (const auto& violation : fresh) {
        std::cout << "- **" << violation.kind << "**: " << violation.detail << "\n";
      }
      if (overLimit) {
        std::cout << "- **max_cycles**: " << after.cycles.size()
                  << " cycles exceeds the configured maximum of "
                  << config.maxCycles << "\n";
      }
    }
  } else if (format == "text") {
    std::cout << "baseline  " << baseline << "  " << shortSha(baselineSha) << "\n";
    std::cout << "target    " << target << "  " << shortSha(targetSha) << "\n\n";
    std::cout << "cycles      " << before.cycles.size() << " -> "
              << after.cycles.size() << "\n";
    std::cout << "violations  " << baselineViolations.size() << " -> "
              << currentViolations.size() << "\n\n";
    if (fresh.empty() && !overLimit) {
      std::cout << "ok: no new architectural regressions\n";
    } else {
      std::cout << "new regressions:\n";
      printViolations(fresh);
      if (overLimit) {
        std::cout << "  x max_cycles: " << after.cycles.size()
                  << " cycles exceeds the configured maximum of "
                  << config.maxCycles << "\n";
      }
      std::cout << "\n" << (fresh.size() + (overLimit ? 1 : 0))
                << " new architectural regression(s)\n";
    }
  } else {
    throw std::runtime_error("unknown --format: " + format);
  }

  return (fresh.empty() && !overLimit) ? 0 : 1;
}

int commandDiff(const Args& args) {
  if (args.positional.size() < 2) {
    throw std::runtime_error("diff needs two revisions: strata diff FROM TO");
  }
  std::string path = args.get("repo", ".");
  std::string format = args.get("format", "text");

  Git git(path);
  Config config = loadConfig(git.root());
  std::string fromSha = git.resolve(args.positional[0]);
  std::string toSha = git.resolve(args.positional[1]);

  std::map<std::string, int> churn;
  Graph before = buildGraph(git, fromSha, config.ignore, churn);
  Graph after = buildGraph(git, toSha, config.ignore, churn);
  GraphDiff diff = diffGraphs(before, after);

  if (format == "json") {
    nlohmann::json out;
    out["from"] = {{"rev", args.positional[0]}, {"sha", fromSha}};
    out["to"] = {{"rev", args.positional[1]}, {"sha", toSha}};
    out["addedNodes"] = diff.addedNodes;
    out["removedNodes"] = diff.removedNodes;
    out["addedEdges"] = nlohmann::json::array();
    for (const auto& [a, b] : diff.addedEdges) {
      out["addedEdges"].push_back({{"from", a}, {"to", b}});
    }
    out["removedEdges"] = nlohmann::json::array();
    for (const auto& [a, b] : diff.removedEdges) {
      out["removedEdges"].push_back({{"from", a}, {"to", b}});
    }
    out["changedNodes"] = nlohmann::json::array();
    for (const auto& change : diff.changedNodes) {
      out["changedNodes"].push_back(
          {{"path", change.path}, {"before", change.locBefore}, {"after", change.locAfter}});
    }
    out["cycles"] = {{"before", before.cycles.size()}, {"after", after.cycles.size()}};
    std::cout << out.dump(2) << "\n";
    return 0;
  }
  if (format != "text") throw std::runtime_error("unknown --format: " + format);

  std::cout << args.positional[0] << " (" << shortSha(fromSha) << ") -> "
            << args.positional[1] << " (" << shortSha(toSha) << ")\n\n";
  std::cout << "+ " << diff.addedNodes.size() << " module(s), - "
            << diff.removedNodes.size() << " module(s), + "
            << diff.addedEdges.size() << " dependency(ies), - "
            << diff.removedEdges.size() << " dependency(ies)\n";
  std::cout << "cycles " << before.cycles.size() << " -> " << after.cycles.size()
            << "\n";

  if (diff.addedNodes.empty() && diff.removedNodes.empty() &&
      diff.addedEdges.empty() && diff.removedEdges.empty() &&
      diff.changedNodes.empty()) {
    std::cout << "\nno architectural change\n";
    return 0;
  }

  std::cout << "\n";
  for (const auto& node : diff.addedNodes) {
    std::cout << "+ module      " << node << "\n";
  }
  for (const auto& node : diff.removedNodes) {
    std::cout << "- module      " << node << "\n";
  }
  for (const auto& [from, to] : diff.addedEdges) {
    std::cout << "+ dependency  " << from << " -> " << to << "\n";
  }
  for (const auto& [from, to] : diff.removedEdges) {
    std::cout << "- dependency  " << from << " -> " << to << "\n";
  }
  for (const auto& change : diff.changedNodes) {
    std::cout << "~ " << change.path << "  " << change.locBefore << " -> "
              << change.locAfter << " lines\n";
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 0;
  }

  std::string command = argv[1];
  try {
    if (command == "version" || command == "--version") {
      std::cout << "strata " STRATA_VERSION "\n";
      return 0;
    }
    if (command == "help" || command == "--help" || command == "-h") {
      usage();
      return 0;
    }
    Args args = parseArgs(argc, argv, 2);
    if (args.has("help")) {
      usage();
      return 0;
    }
    if (command == "ingest") return commandIngest(args);
    if (command == "serve") return commandServe(args);
    if (command == "check") return commandCheck(args);
    if (command == "diff") return commandDiff(args);
    std::cerr << "strata: unknown command '" << command << "'\n\n";
    usage();
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "strata: " << error.what() << "\n";
    return 2;
  }
}
