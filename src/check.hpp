#pragma once

#include <string>
#include <vector>

#include "config.hpp"
#include "graph.hpp"

namespace strata {

struct Violation {
  std::string kind;    // "cycle" or "layer"
  std::string key;     // stable identity, used for the ratchet
  std::string detail;  // human-readable explanation
};

std::vector<Violation> findViolations(const Graph& graph, const Config& config);

// Violations in `current` whose key did not appear in `baseline`.
std::vector<Violation> newViolations(const std::vector<Violation>& baseline,
                                     const std::vector<Violation>& current);

}  // namespace strata
