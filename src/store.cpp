#include "store.hpp"

#include <sqlite3.h>

#include <ctime>
#include <stdexcept>

#include "util.hpp"

namespace strata {

namespace {

class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql) : db_(db) {
    if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
      throw std::runtime_error(std::string("sqlite prepare failed: ") +
                               sqlite3_errmsg(db));
    }
  }

  ~Stmt() { sqlite3_finalize(stmt_); }

  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;

  void bind(int index, const std::string& value) {
    if (sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT) !=
        SQLITE_OK) {
      throw std::runtime_error(std::string("sqlite bind failed: ") +
                               sqlite3_errmsg(db_));
    }
  }

  void bind(int index, int value) {
    if (sqlite3_bind_int(stmt_, index, value) != SQLITE_OK) {
      throw std::runtime_error(std::string("sqlite bind failed: ") +
                               sqlite3_errmsg(db_));
    }
  }

  void bind(int index, long long value) {
    if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) {
      throw std::runtime_error(std::string("sqlite bind failed: ") +
                               sqlite3_errmsg(db_));
    }
  }

  bool step() {
    int status = sqlite3_step(stmt_);
    if (status == SQLITE_ROW) return true;
    if (status == SQLITE_DONE) return false;
    throw std::runtime_error(std::string("sqlite step failed: ") +
                             sqlite3_errmsg(db_));
  }

  sqlite3_stmt* get() { return stmt_; }

 private:
  sqlite3* db_ = nullptr;
  sqlite3_stmt* stmt_ = nullptr;
};

void exec(sqlite3* db, const char* sql) {
  char* error = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
    std::string message = error ? error : "unknown error";
    sqlite3_free(error);
    throw std::runtime_error("sqlite exec failed: " + message);
  }
}

std::string text(sqlite3_stmt* stmt, int column) {
  const unsigned char* value = sqlite3_column_text(stmt, column);
  return value ? reinterpret_cast<const char*>(value) : "";
}

SnapshotRow readSnapshot(sqlite3_stmt* stmt) {
  SnapshotRow row;
  row.id = sqlite3_column_int(stmt, 0);
  row.seq = sqlite3_column_int(stmt, 1);
  row.sha = text(stmt, 2);
  row.time = sqlite3_column_int64(stmt, 3);
  row.subject = text(stmt, 4);
  row.parents = text(stmt, 5);
  row.nodes = sqlite3_column_int(stmt, 6);
  row.edges = sqlite3_column_int(stmt, 7);
  row.cycles = sqlite3_column_int(stmt, 8);
  row.loc = sqlite3_column_int(stmt, 9);
  return row;
}

const char* kSnapshotColumns =
    "id, seq, sha, time, subject, parents, nodes, edges, cycles, loc";

}  // namespace

Store::Store(const std::string& path) {
  int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  if (sqlite3_open_v2(path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
    std::string message = db_ ? sqlite3_errmsg(db_) : "cannot open database";
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error("cannot open " + path + ": " + message);
  }
  sqlite3_busy_timeout(db_, 5000);
}

Store::~Store() {
  if (db_ != nullptr) sqlite3_close(db_);
}

void Store::init() {
  exec(db_,
       "PRAGMA journal_mode=WAL;"
       "PRAGMA foreign_keys=ON;"
       "PRAGMA synchronous=NORMAL;");
  exec(db_,
       "CREATE TABLE IF NOT EXISTS repo ("
       "  id INTEGER PRIMARY KEY,"
       "  path TEXT NOT NULL UNIQUE,"
       "  name TEXT NOT NULL,"
       "  head TEXT NOT NULL,"
       "  ingested_at INTEGER NOT NULL"
       ");"
       "CREATE TABLE IF NOT EXISTS snapshot ("
       "  id INTEGER PRIMARY KEY,"
       "  repo_id INTEGER NOT NULL REFERENCES repo(id) ON DELETE CASCADE,"
       "  seq INTEGER NOT NULL,"
       "  sha TEXT NOT NULL,"
       "  time INTEGER NOT NULL,"
       "  subject TEXT NOT NULL,"
       "  parents TEXT NOT NULL,"
       "  nodes INTEGER NOT NULL,"
       "  edges INTEGER NOT NULL,"
       "  cycles INTEGER NOT NULL,"
       "  loc INTEGER NOT NULL,"
       "  UNIQUE(repo_id, seq),"
       "  UNIQUE(repo_id, sha)"
       ");"
       "CREATE TABLE IF NOT EXISTS node ("
       "  snapshot_id INTEGER NOT NULL REFERENCES snapshot(id) ON DELETE CASCADE,"
       "  path TEXT NOT NULL,"
       "  loc INTEGER NOT NULL,"
       "  files INTEGER NOT NULL,"
       "  churn INTEGER NOT NULL,"
       "  lang TEXT NOT NULL,"
       "  PRIMARY KEY (snapshot_id, path)"
       ") WITHOUT ROWID;"
       "CREATE TABLE IF NOT EXISTS edge ("
       "  snapshot_id INTEGER NOT NULL REFERENCES snapshot(id) ON DELETE CASCADE,"
       "  src TEXT NOT NULL,"
       "  dst TEXT NOT NULL,"
       "  weight INTEGER NOT NULL,"
       "  PRIMARY KEY (snapshot_id, src, dst)"
       ") WITHOUT ROWID;"
       "CREATE INDEX IF NOT EXISTS edge_pair ON edge(src, dst);"
       "CREATE INDEX IF NOT EXISTS snapshot_seq ON snapshot(repo_id, seq);");
}

int Store::upsertRepo(const std::string& path, const std::string& name,
                      const std::string& head) {
  Stmt stmt(db_,
            "INSERT INTO repo(path, name, head, ingested_at) VALUES(?, ?, ?, ?) "
            "ON CONFLICT(path) DO UPDATE SET "
            "name=excluded.name, head=excluded.head, ingested_at=excluded.ingested_at "
            "RETURNING id");
  stmt.bind(1, path);
  stmt.bind(2, name);
  stmt.bind(3, head);
  stmt.bind(4, static_cast<long long>(std::time(nullptr)));
  if (!stmt.step()) throw std::runtime_error("upsert repo returned no row");
  return sqlite3_column_int(stmt.get(), 0);
}

void Store::clearSnapshots(int repoId) {
  Stmt stmt(db_, "DELETE FROM snapshot WHERE repo_id = ?");
  stmt.bind(1, repoId);
  stmt.step();
}

void Store::addSnapshot(int repoId, int seq, const std::string& sha,
                        long long time, const std::string& subject,
                        const std::string& parents, const Graph& graph) {
  exec(db_, "BEGIN");
  try {
    int snapshotId = 0;
    {
      Stmt stmt(db_,
                "INSERT INTO snapshot(repo_id, seq, sha, time, subject, parents, "
                "nodes, edges, cycles, loc) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
      stmt.bind(1, repoId);
      stmt.bind(2, seq);
      stmt.bind(3, sha);
      stmt.bind(4, time);
      stmt.bind(5, subject);
      stmt.bind(6, parents);
      stmt.bind(7, static_cast<int>(graph.nodes.size()));
      stmt.bind(8, graph.edgeCount());
      stmt.bind(9, static_cast<int>(graph.cycles.size()));
      stmt.bind(10, graph.totalLoc());
      stmt.step();
      snapshotId = static_cast<int>(sqlite3_last_insert_rowid(db_));
    }

    {
      Stmt stmt(db_,
                "INSERT INTO node(snapshot_id, path, loc, files, churn, lang) "
                "VALUES(?, ?, ?, ?, ?, ?)");
      for (const auto& [path, node] : graph.nodes) {
        stmt.bind(1, snapshotId);
        stmt.bind(2, path);
        stmt.bind(3, node.loc);
        stmt.bind(4, node.files);
        stmt.bind(5, node.churn);
        stmt.bind(6, node.lang);
        stmt.step();
        sqlite3_reset(stmt.get());
      }
    }

    {
      Stmt stmt(db_,
                "INSERT INTO edge(snapshot_id, src, dst, weight) VALUES(?, ?, ?, ?)");
      for (const auto& [key, edge] : graph.edges) {
        (void)key;
        stmt.bind(1, snapshotId);
        stmt.bind(2, edge.from);
        stmt.bind(3, edge.to);
        stmt.bind(4, edge.weight);
        stmt.step();
        sqlite3_reset(stmt.get());
      }
    }

    exec(db_, "COMMIT");
  } catch (...) {
    exec(db_, "ROLLBACK");
    throw;
  }
}

std::optional<RepoRow> Store::repo() const {
  Stmt stmt(db_,
            "SELECT id, path, name, head, ingested_at FROM repo ORDER BY id LIMIT 1");
  if (!stmt.step()) return std::nullopt;
  RepoRow row;
  row.id = sqlite3_column_int(stmt.get(), 0);
  row.path = text(stmt.get(), 1);
  row.name = text(stmt.get(), 2);
  row.head = text(stmt.get(), 3);
  row.ingestedAt = sqlite3_column_int64(stmt.get(), 4);
  return row;
}

int Store::repoId() const {
  auto row = repo();
  if (!row) throw std::runtime_error("no repository ingested in this database");
  return row->id;
}

std::vector<SnapshotRow> Store::snapshots(int repoId) const {
  Stmt stmt(db_,
            (std::string("SELECT ") + kSnapshotColumns +
             " FROM snapshot WHERE repo_id = ? ORDER BY seq")
                .c_str());
  stmt.bind(1, repoId);
  std::vector<SnapshotRow> rows;
  while (stmt.step()) rows.push_back(readSnapshot(stmt.get()));
  return rows;
}

std::optional<SnapshotRow> Store::snapshot(int id) const {
  Stmt stmt(db_, (std::string("SELECT ") + kSnapshotColumns +
                  " FROM snapshot WHERE id = ?")
                     .c_str());
  stmt.bind(1, id);
  if (!stmt.step()) return std::nullopt;
  return readSnapshot(stmt.get());
}

std::optional<SnapshotRow> Store::snapshotBySha(const std::string& sha) const {
  Stmt stmt(db_, (std::string("SELECT ") + kSnapshotColumns +
                  " FROM snapshot WHERE sha LIKE ? ORDER BY id DESC LIMIT 1")
                     .c_str());
  stmt.bind(1, sha + "%");
  if (!stmt.step()) return std::nullopt;
  return readSnapshot(stmt.get());
}

std::optional<int> Store::previousSnapshotId(int snapshotId) const {
  int repoIdValue = 0;
  int seq = 0;
  {
    Stmt stmt(db_, "SELECT repo_id, seq FROM snapshot WHERE id = ?");
    stmt.bind(1, snapshotId);
    if (!stmt.step()) return std::nullopt;
    repoIdValue = sqlite3_column_int(stmt.get(), 0);
    seq = sqlite3_column_int(stmt.get(), 1);
  }
  if (seq <= 0) return std::nullopt;

  Stmt stmt(db_, "SELECT id FROM snapshot WHERE repo_id = ? AND seq = ?");
  stmt.bind(1, repoIdValue);
  stmt.bind(2, seq - 1);
  if (!stmt.step()) return std::nullopt;
  return sqlite3_column_int(stmt.get(), 0);
}

Graph Store::graph(int snapshotId) const {
  Graph graph;

  {
    Stmt stmt(db_,
              "SELECT path, loc, files, churn, lang FROM node WHERE snapshot_id = ?");
    stmt.bind(1, snapshotId);
    while (stmt.step()) {
      Node node;
      node.path = text(stmt.get(), 0);
      node.loc = sqlite3_column_int(stmt.get(), 1);
      node.files = sqlite3_column_int(stmt.get(), 2);
      node.churn = sqlite3_column_int(stmt.get(), 3);
      node.lang = text(stmt.get(), 4);
      graph.nodes.emplace(node.path, node);
    }
  }

  {
    Stmt stmt(db_, "SELECT src, dst, weight FROM edge WHERE snapshot_id = ?");
    stmt.bind(1, snapshotId);
    while (stmt.step()) {
      Edge edge;
      edge.from = text(stmt.get(), 0);
      edge.to = text(stmt.get(), 1);
      edge.weight = sqlite3_column_int(stmt.get(), 2);
      graph.edges.emplace(std::make_pair(edge.from, edge.to), edge);
    }
  }

  graph.computeCycles();
  return graph;
}

std::map<std::pair<std::string, std::string>, int> Store::edgeFirstSeqs(
    int repoIdValue) const {
  std::map<std::pair<std::string, std::string>, int> out;
  Stmt stmt(db_,
            "SELECT e.src, e.dst, MIN(s.seq) FROM edge e "
            "JOIN snapshot s ON s.id = e.snapshot_id "
            "WHERE s.repo_id = ? GROUP BY e.src, e.dst");
  stmt.bind(1, repoIdValue);
  while (stmt.step()) {
    out[std::make_pair(text(stmt.get(), 0), text(stmt.get(), 1))] =
        sqlite3_column_int(stmt.get(), 2);
  }
  return out;
}

std::map<std::string, int> Store::nodeFirstSeqs(int repoIdValue) const {
  std::map<std::string, int> out;
  Stmt stmt(db_,
            "SELECT n.path, MIN(s.seq) FROM node n "
            "JOIN snapshot s ON s.id = n.snapshot_id "
            "WHERE s.repo_id = ? GROUP BY n.path");
  stmt.bind(1, repoIdValue);
  while (stmt.step()) {
    out[text(stmt.get(), 0)] = sqlite3_column_int(stmt.get(), 1);
  }
  return out;
}

std::map<std::pair<std::string, std::string>, int> Store::edgeLastSeqs(
    int repoIdValue) const {
  std::map<std::pair<std::string, std::string>, int> out;
  Stmt stmt(db_,
            "SELECT e.src, e.dst, MAX(s.seq) FROM edge e "
            "JOIN snapshot s ON s.id = e.snapshot_id "
            "WHERE s.repo_id = ? GROUP BY e.src, e.dst");
  stmt.bind(1, repoIdValue);
  while (stmt.step()) {
    out[std::make_pair(text(stmt.get(), 0), text(stmt.get(), 1))] =
        sqlite3_column_int(stmt.get(), 2);
  }
  return out;
}

}  // namespace strata
