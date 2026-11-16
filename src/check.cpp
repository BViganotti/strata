#include "check.hpp"

#include <algorithm>
#include <set>

#include "util.hpp"

namespace strata {

std::vector<Violation> findViolations(const Graph& graph, const Config& config) {
  std::vector<Violation> violations;

  for (const auto& cycle : graph.cycles) {
    std::vector<std::string> members = cycle;
    std::sort(members.begin(), members.end());
    Violation violation;
    violation.kind = "cycle";
    violation.key = "cycle:" + [&] {
      std::string joined;
      for (size_t i = 0; i < members.size(); ++i) {
        if (i) joined += "|";
        joined += members[i];
      }
      return joined;
    }();
    violation.detail = "cycle: ";
    for (size_t i = 0; i < cycle.size(); ++i) {
      if (i) violation.detail += " -> ";
      violation.detail += cycle[i];
    }
    violation.detail += " -> " + cycle.front();
    violations.push_back(std::move(violation));
  }

  if (!config.allow.empty()) {
    for (const auto& [key, edge] : graph.edges) {
      std::string fromLayer = config.layerFor(edge.from);
      std::string toLayer = config.layerFor(edge.to);
      if (fromLayer.empty() || toLayer.empty()) continue;
      if (config.permits(fromLayer, toLayer)) continue;

      Violation violation;
      violation.kind = "layer";
      violation.key = "layer:" + edge.from + "->" + edge.to;
      violation.detail = edge.from + " (" + fromLayer + ") -> " + edge.to + " (" +
                         toLayer + "): " + fromLayer + " may not depend on " +
                         toLayer;
      violations.push_back(std::move(violation));
    }
  }

  std::sort(violations.begin(), violations.end(),
            [](const Violation& a, const Violation& b) { return a.key < b.key; });
  return violations;
}

std::vector<Violation> newViolations(const std::vector<Violation>& baseline,
                                     const std::vector<Violation>& current) {
  std::set<std::string> known;
  for (const auto& violation : baseline) known.insert(violation.key);

  std::vector<Violation> fresh;
  for (const auto& violation : current) {
    if (!known.count(violation.key)) fresh.push_back(violation);
  }
  return fresh;
}

}  // namespace strata
