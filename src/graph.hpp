#pragma once

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace strata {

struct Node {
  std::string path;
  int loc = 0;
  int files = 0;
  int churn = 0;
  std::string lang;
};

struct Edge {
  std::string from;
  std::string to;
  int weight = 1;
};

struct Graph {
  std::map<std::string, Node> nodes;
  std::map<std::pair<std::string, std::string>, Edge> edges;
  std::vector<std::vector<std::string>> cycles;

  int totalLoc() const;
  int edgeCount() const;
  Node& addNode(const std::string& path);
  void addEdge(const std::string& from, const std::string& to, int weight);
  void computeCycles();

  std::set<std::string> cycleNodes() const;
  std::set<std::pair<std::string, std::string>> cycleEdges() const;
};

struct GraphDiff {
  std::vector<std::string> addedNodes;
  std::vector<std::string> removedNodes;
  std::vector<std::pair<std::string, std::string>> addedEdges;
  std::vector<std::pair<std::string, std::string>> removedEdges;
  struct NodeDelta {
    std::string path;
    int locBefore = 0;
    int locAfter = 0;
  };
  std::vector<NodeDelta> changedNodes;

  bool empty() const;
};

GraphDiff diffGraphs(const Graph& before, const Graph& after);

std::string dirName(const std::string& path);
std::string baseName(const std::string& path);
std::string normalizePath(const std::string& path);
std::string joinPath(const std::string& a, const std::string& b);

}  // namespace strata
