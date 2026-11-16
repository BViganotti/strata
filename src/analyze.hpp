#pragma once

#include <map>
#include <string>
#include <vector>

#include "git.hpp"
#include "graph.hpp"

namespace strata {

// Rebuilds the directory-level dependency graph of one commit. `churn` is the
// cumulative number of commits that touched each directory up to and
// including this commit; it is attached to the resulting nodes.
Graph buildGraph(const Git& git, const std::string& sha,
                 const std::vector<std::string>& ignore,
                 const std::map<std::string, int>& churn);

}  // namespace strata
