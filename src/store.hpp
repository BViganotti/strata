#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "graph.hpp"

struct sqlite3;

namespace strata {

struct RepoRow {
  int id = 0;
  std::string path;
  std::string name;
  std::string head;
  long long ingestedAt = 0;
};

struct SnapshotRow {
  int id = 0;
  int seq = 0;
  std::string sha;
  long long time = 0;
  std::string subject;
  std::string parents;
  int nodes = 0;
  int edges = 0;
  int cycles = 0;
  int loc = 0;
};

class Store {
 public:
  explicit Store(const std::string& path);
  ~Store();

  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;

  void init();
  int upsertRepo(const std::string& path, const std::string& name,
                 const std::string& head);
  void clearSnapshots(int repoId);
  void addSnapshot(int repoId, int seq, const std::string& sha, long long time,
                   const std::string& subject, const std::string& parents,
                   const Graph& graph);

  std::optional<RepoRow> repo() const;
  std::vector<SnapshotRow> snapshots(int repoId) const;
  std::optional<SnapshotRow> snapshot(int id) const;
  std::optional<SnapshotRow> snapshotBySha(const std::string& sha) const;
  std::optional<int> previousSnapshotId(int snapshotId) const;
  Graph graph(int snapshotId) const;

  int repoId() const;

  std::map<std::pair<std::string, std::string>, int> edgeFirstSeqs(int repoId) const;
  std::map<std::string, int> nodeFirstSeqs(int repoId) const;
  std::map<std::pair<std::string, std::string>, int> edgeLastSeqs(int repoId) const;

 private:
  sqlite3* db_ = nullptr;
};

}  // namespace strata
