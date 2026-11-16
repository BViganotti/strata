#pragma once

#include <map>
#include <string>

namespace strata {

// Serves the embedded UI and the JSON API. `webDir` overrides the embedded
// assets when non-empty, which is handy while editing the frontend.
void serve(const std::string& dbPath, const std::string& addr,
           const std::string& webDir);

// Defined in the generated web_assets.cpp.
const std::map<std::string, std::string>& webAssets();

}  // namespace strata
