#include "graph.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <set>

#include "util.hpp"

namespace strata {

int Graph::totalLoc() const {
  int total = 0;
  for (const auto& [path, node] : nodes) total += node.loc;
  return total;
}

int Graph::edgeCount() const { return static_cast<int>(edges.size()); }

Node& Graph::addNode(const std::string& path) {
  auto [it, inserted] = nodes.emplace(path, Node{});
  if (inserted) it->second.path = path;
  return it->second;
}

void Graph::addEdge(const std::string& from, const std::string& to, int weight) {
  if (from.empty() || to.empty() || from == to) return;
  auto key = std::make_pair(from, to);
  auto it = edges.find(key);
  if (it == edges.end()) {
    Edge e;
    e.from = from;
    e.to = to;
    e.weight = weight;
    edges.emplace(key, e);
  } else {
    it->second.weight += weight;
  }
}

void Graph::computeCycles() {
  cycles.clear();

  // Tarjan's strongly connected components.
  std::map<std::string, int> index;
  std::map<std::string, int> low;
  std::set<std::string> onStack;
  std::vector<std::string> stack;
  int nextIndex = 0;

  std::map<std::string, std::vector<std::string>> adjacency;
  for (const auto& [key, edge] : edges) {
    adjacency[edge.from].push_back(edge.to);
    // Make sure both endpoints are visited even when they are isolated.
    nodes.emplace(edge.to, Node{edge.to, 0, 0, 0, ""});
  }

  std::function<void(const std::string&)> visit = [&](const std::string& v) {
    index[v] = low[v] = nextIndex++;
    stack.push_back(v);
    onStack.insert(v);

    for (const std::string& w : adjacency[v]) {
      if (!index.count(w)) {
        visit(w);
        low[v] = std::min(low[v], low[w]);
      } else if (onStack.count(w)) {
        low[v] = std::min(low[v], index[w]);
      }
    }

    if (low[v] == index[v]) {
      std::vector<std::string> component;
      while (true) {
        std::string w = stack.back();
        stack.pop_back();
        onStack.erase(w);
        component.push_back(w);
        if (w == v) break;
      }
      if (component.size() > 1) {
        std::sort(component.begin(), component.end());
        cycles.push_back(component);
      }
    }
  };

  for (const auto& [path, node] : nodes) {
    (void)node;
    if (!index.count(path)) visit(path);
  }

  std::sort(cycles.begin(), cycles.end());
}

std::set<std::string> Graph::cycleNodes() const {
  std::set<std::string> out;
  for (const auto& cycle : cycles) {
    out.insert(cycle.begin(), cycle.end());
  }
  return out;
}

std::set<std::pair<std::string, std::string>> Graph::cycleEdges() const {
  std::set<std::pair<std::string, std::string>> out;
  std::set<std::string> cyclic = cycleNodes();
  for (const auto& [key, edge] : edges) {
    if (cyclic.count(edge.from) && cyclic.count(edge.to)) out.insert(key);
  }
  return out;
}

bool GraphDiff::empty() const {
  return addedNodes.empty() && removedNodes.empty() && addedEdges.empty() &&
         removedEdges.empty() && changedNodes.empty();
}

GraphDiff diffGraphs(const Graph& before, const Graph& after) {
  GraphDiff diff;

  for (const auto& [path, node] : after.nodes) {
    auto it = before.nodes.find(path);
    if (it == before.nodes.end()) {
      diff.addedNodes.push_back(path);
    } else if (it->second.loc != node.loc) {
      diff.changedNodes.push_back({path, it->second.loc, node.loc});
    }
  }
  for (const auto& [path, node] : before.nodes) {
    (void)node;
    if (!after.nodes.count(path)) diff.removedNodes.push_back(path);
  }

  for (const auto& [key, edge] : after.edges) {
    (void)edge;
    if (!before.edges.count(key)) diff.addedEdges.push_back(key);
  }
  for (const auto& [key, edge] : before.edges) {
    (void)edge;
    if (!after.edges.count(key)) diff.removedEdges.push_back(key);
  }

  return diff;
}

std::string normalizePath(const std::string& path) {
  if (path.empty()) return ".";
  auto normalized = std::filesystem::path(path).lexically_normal().generic_string();
  while (startsWith(normalized, "./")) normalized = normalized.substr(2);
  if (normalized.empty()) return ".";
  if (normalized.size() > 1 && normalized.back() == '/') normalized.pop_back();
  return normalized;
}

std::string dirName(const std::string& path) {
  return normalizePath(std::filesystem::path(path).parent_path().generic_string());
}

std::string baseName(const std::string& path) {
  if (path.empty() || path == ".") return ".";
  std::string name = std::filesystem::path(path).filename().generic_string();
  if (name.empty()) return path;
  return name;
}

std::string joinPath(const std::string& a, const std::string& b) {
  if (a.empty() || a == ".") return normalizePath(b);
  if (b.empty()) return normalizePath(a);
  return normalizePath((std::filesystem::path(a) / b).generic_string());
}

}  // namespace strata
