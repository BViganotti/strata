#include "web.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>

#include <httplib/httplib.h>
#include <nlohmann/json.hpp>

#include "check.hpp"
#include "config.hpp"
#include "store.hpp"
#include "util.hpp"

#ifndef STRATA_VERSION
#define STRATA_VERSION "0.1.0"
#endif

namespace strata {

namespace {

using json = nlohmann::json;

std::string mimeFor(const std::string& path) {
  if (endsWith(path, ".html")) return "text/html; charset=utf-8";
  if (endsWith(path, ".js")) return "text/javascript; charset=utf-8";
  if (endsWith(path, ".css")) return "text/css; charset=utf-8";
  if (endsWith(path, ".svg")) return "image/svg+xml";
  if (endsWith(path, ".png")) return "image/png";
  if (endsWith(path, ".ico")) return "image/x-icon";
  if (endsWith(path, ".json")) return "application/json";
  return "application/octet-stream";
}

json pairList(const std::vector<std::pair<std::string, std::string>>& values) {
  json out = json::array();
  for (const auto& [a, b] : values) out.push_back(json::array({a, b}));
  return out;
}

void sendJson(httplib::Response& res, const json& body) {
  res.set_header("Cache-Control", "no-store");
  res.set_content(body.dump(), "application/json");
}

void sendError(httplib::Response& res, int status, const std::string& message) {
  res.status = status;
  sendJson(res, json{{"error", message}});
}

std::pair<std::string, int> parseAddress(const std::string& address) {
  size_t colon = address.find_last_of(':');
  if (colon == std::string::npos) {
    return {"127.0.0.1", std::stoi(address)};
  }
  std::string host = address.substr(0, colon);
  int port = std::stoi(address.substr(colon + 1));
  if (host.empty() || host == "*") host = "0.0.0.0";
  return {host, port};
}

}  // namespace

void serve(const std::string& dbPath, const std::string& addr,
           const std::string& webDir) {
  auto store = std::make_shared<Store>(dbPath);
  store->init();

  auto repoOpt = store->repo();
  if (!repoOpt) {
    throw std::runtime_error(
        "no repository in " + dbPath + "; run: strata ingest <path> --db " + dbPath);
  }
  const RepoRow repo = *repoOpt;
  Config config = loadConfig(repo.path);

  std::vector<SnapshotRow> snapshots = store->snapshots(repo.id);
  if (snapshots.empty()) {
    throw std::runtime_error("the database has no snapshots; re-run strata ingest");
  }
  const std::map<std::string, int> nodeFirst = store->nodeFirstSeqs(repo.id);
  const std::map<std::pair<std::string, std::string>, int> edgeFirst =
      store->edgeFirstSeqs(repo.id);

  json meta;
  meta["repo"] = {{"name", repo.name}, {"path", repo.path}, {"head", repo.head}};
  {
    json layers = json::array();
    for (const auto& layer : config.layers) {
      layers.push_back({{"name", layer.name}, {"paths", layer.paths}});
    }
    meta["config"] = {{"layers", layers},
                      {"ignore", config.ignore},
                      {"maxCycles", config.maxCycles}};
  }
  {
    json commits = json::array();
    for (const auto& snapshot : snapshots) {
      commits.push_back({{"sha", snapshot.sha},
                         {"short", snapshot.sha.substr(0, 7)},
                         {"time", snapshot.time},
                         {"subject", snapshot.subject},
                         {"nodes", snapshot.nodes},
                         {"edges", snapshot.edges},
                         {"cycles", snapshot.cycles},
                         {"loc", snapshot.loc}});
    }
    meta["commits"] = std::move(commits);
  }

  std::mutex mutex;
  httplib::Server server;

  server.Get("/api/meta", [&](const httplib::Request&, httplib::Response& res) {
    sendJson(res, meta);
  });

  server.Get("/api/graph", [&](const httplib::Request& req, httplib::Response& res) {
    try {
      std::string sha = req.get_param_value("sha");
      if (sha.empty()) sha = repo.head;

      std::lock_guard<std::mutex> lock(mutex);
      auto snapshot = store->snapshotBySha(sha);
      if (!snapshot) {
        sendError(res, 404, "unknown commit: " + sha);
        return;
      }

      Graph graph = store->graph(snapshot->id);
      std::optional<SnapshotRow> previous;
      if (auto previousId = store->previousSnapshotId(snapshot->id)) {
        previous = store->snapshot(*previousId);
      }

      std::set<std::string> previousNodes;
      std::set<std::pair<std::string, std::string>> previousEdges;
      GraphDiff diff;
      if (previous) {
        Graph before = store->graph(previous->id);
        for (const auto& [path, node] : before.nodes) {
          (void)node;
          previousNodes.insert(path);
        }
        for (const auto& [key, edge] : before.edges) {
          (void)edge;
          previousEdges.insert(key);
        }
        diff = diffGraphs(before, graph);
      }

      const std::set<std::string> cycleNodes = graph.cycleNodes();
      const std::set<std::pair<std::string, std::string>> cycleEdges =
          graph.cycleEdges();
      const std::vector<Violation> violations = findViolations(graph, config);

      json body;
      body["sha"] = snapshot->sha;
      body["short"] = snapshot->sha.substr(0, 7);
      body["seq"] = snapshot->seq;
      body["subject"] = snapshot->subject;
      body["time"] = snapshot->time;
      body["previous"] = previous ? json(previous->seq) : json(nullptr);

      json nodes = json::array();
      for (const auto& [path, node] : graph.nodes) {
        auto first = nodeFirst.find(path);
        std::string label = path == "." ? "(root)" : path;
        nodes.push_back({{"id", path},
                         {"label", label},
                         {"loc", node.loc},
                         {"files", node.files},
                         {"churn", node.churn},
                         {"lang", node.lang},
                         {"layer", config.layerFor(path)},
                         {"cycle", cycleNodes.count(path) > 0},
                         {"added", previous && !previousNodes.count(path)},
                         {"since", first == nodeFirst.end() ? snapshot->seq
                                                            : first->second}});
      }
      body["nodes"] = std::move(nodes);

      json edges = json::array();
      for (const auto& [key, edge] : graph.edges) {
        auto first = edgeFirst.find(key);
        edges.push_back({{"from", edge.from},
                         {"to", edge.to},
                         {"weight", edge.weight},
                         {"cycle", cycleEdges.count(key) > 0},
                         {"added", previous && !previousEdges.count(key)},
                         {"since", first == edgeFirst.end() ? snapshot->seq
                                                            : first->second}});
      }
      body["edges"] = std::move(edges);

      json cycleList = json::array();
      for (const auto& cycle : graph.cycles) cycleList.push_back(cycle);
      body["cycles"] = std::move(cycleList);

      json violationList = json::array();
      for (const auto& violation : violations) {
        violationList.push_back({{"kind", violation.kind},
                                 {"key", violation.key},
                                 {"detail", violation.detail}});
      }
      body["violations"] = std::move(violationList);

      body["diff"] = {{"addedNodes", diff.addedNodes},
                      {"removedNodes", diff.removedNodes},
                      {"addedEdges", pairList(diff.addedEdges)},
                      {"removedEdges", pairList(diff.removedEdges)}};
      body["stats"] = {{"nodes", graph.nodes.size()},
                       {"edges", graph.edges.size()},
                       {"cycles", graph.cycles.size()},
                       {"loc", graph.totalLoc()},
                       {"violations", violations.size()}};

      sendJson(res, body);
    } catch (const std::exception& error) {
      sendError(res, 500, error.what());
    }
  });

  auto serveAsset = [&](const httplib::Request& req, httplib::Response& res) {
    std::string path = req.path;
    if (path == "/") path = "/index.html";
    if (path.find("..") != std::string::npos) {
      res.status = 404;
      res.set_content("not found", "text/plain");
      return;
    }

    std::string body;
    if (!webDir.empty()) {
      std::ifstream in(webDir + path, std::ios::binary);
      if (in) {
        std::ostringstream buffer;
        buffer << in.rdbuf();
        body = buffer.str();
      }
    }
    if (body.empty()) {
      const auto& assets = webAssets();
      auto it = assets.find(path);
      if (it != assets.end()) body = it->second;
    }
    if (body.empty()) {
      res.status = 404;
      res.set_content("not found", "text/plain");
      return;
    }
    res.set_header("Cache-Control", "no-cache");
    res.set_content(body, mimeFor(path));
  };

  server.Get("/", serveAsset);
  for (const auto& [path, content] : webAssets()) {
    (void)content;
    server.Get(path, serveAsset);
  }
  server.Get(R"(/.*)", serveAsset);

  const auto [host, port] = parseAddress(addr);
  std::cout << "strata " STRATA_VERSION "\n";
  std::cout << "repo   " << repo.name << " (" << snapshots.size() << " snapshots)\n";
  std::cout << "serving http://" << (host == "0.0.0.0" ? std::string("localhost") : host)
            << ":" << port << "\n";
  std::cout.flush();

  if (!server.listen(host, port)) {
    throw std::runtime_error("cannot listen on " + addr);
  }
}

}  // namespace strata
