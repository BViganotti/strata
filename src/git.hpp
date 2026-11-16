#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace strata {

struct Commit {
  std::string sha;
  std::string subject;
  long long time = 0;
  std::vector<std::string> parents;
};

struct TreeEntry {
  std::string path;
  std::string blob;
};

// Runs a git command inside `repo` and returns stdout. Throws
// std::runtime_error with git's stderr when the command fails.
std::string runGit(const std::string& repo, const std::vector<std::string>& args);

class Git {
 public:
  explicit Git(std::string repo);
  ~Git();

  Git(const Git&) = delete;
  Git& operator=(const Git&) = delete;

  const std::string& root() const { return root_; }
  std::string head() const;
  std::string resolve(const std::string& rev) const;
  std::string subject(const std::string& rev) const;
  std::string remoteName() const;

  // First-parent history in chronological order, evenly sampled down to
  // `limit` commits when the history is longer.
  std::vector<Commit> log(int limit, int stride, const std::string& since) const;

  std::vector<TreeEntry> lsTree(const std::string& sha) const;
  std::vector<std::string> changedFiles(const std::string& sha) const;

  // Blob contents through a long-lived `git cat-file --batch` process.
  std::string readBlob(const std::string& blob) const;

 private:
  struct Batch;
  std::string root_;
  mutable std::unique_ptr<Batch> batch_;
};

}  // namespace strata
