#include "git.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "graph.hpp"
#include "util.hpp"

namespace strata {

namespace {

std::string readAll(int fd) {
  std::string out;
  char buffer[65536];
  while (true) {
    ssize_t n = ::read(fd, buffer, sizeof(buffer));
    if (n == 0) break;
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    out.append(buffer, static_cast<size_t>(n));
  }
  return out;
}

void writeAll(int fd, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    ssize_t n = ::write(fd, data.data() + sent, data.size() - sent);
    if (n < 0) {
      if (errno == EINTR) continue;
      throw std::runtime_error(std::string("write failed: ") + std::strerror(errno));
    }
    sent += static_cast<size_t>(n);
  }
}

}  // namespace

std::string runGit(const std::string& repo, const std::vector<std::string>& args) {
  int outPipe[2];
  int errPipe[2];
  if (::pipe(outPipe) != 0 || ::pipe(errPipe) != 0) {
    throw std::runtime_error("cannot create pipes");
  }

  pid_t pid = ::fork();
  if (pid < 0) throw std::runtime_error("cannot fork git");

  if (pid == 0) {
    ::dup2(outPipe[1], STDOUT_FILENO);
    ::dup2(errPipe[1], STDERR_FILENO);
    ::close(outPipe[0]);
    ::close(outPipe[1]);
    ::close(errPipe[0]);
    ::close(errPipe[1]);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("git"));
    argv.push_back(const_cast<char*>("-C"));
    argv.push_back(const_cast<char*>(repo.c_str()));
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    ::execvp("git", argv.data());
    _exit(127);
  }

  ::close(outPipe[1]);
  ::close(errPipe[1]);
  std::string out = readAll(outPipe[0]);
  std::string err = readAll(errPipe[0]);
  ::close(outPipe[0]);
  ::close(errPipe[0]);

  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    std::string message = "git";
    for (const auto& arg : args) message += " " + arg;
    message += " failed";
    if (!trim(err).empty()) message += ": " + trim(err);
    throw std::runtime_error(message);
  }
  return out;
}

struct Git::Batch {
  pid_t pid = -1;
  int input = -1;
  FILE* output = nullptr;

  explicit Batch(const std::string& repo) {
    std::signal(SIGPIPE, SIG_IGN);

    int inPipe[2];
    int outPipe[2];
    if (::pipe(inPipe) != 0 || ::pipe(outPipe) != 0) {
      throw std::runtime_error("cannot create cat-file pipes");
    }

    pid = ::fork();
    if (pid < 0) throw std::runtime_error("cannot fork cat-file");

    if (pid == 0) {
      ::dup2(inPipe[0], STDIN_FILENO);
      ::dup2(outPipe[1], STDOUT_FILENO);
      ::close(inPipe[0]);
      ::close(inPipe[1]);
      ::close(outPipe[0]);
      ::close(outPipe[1]);
      ::execlp("git", "git", "-C", repo.c_str(), "cat-file", "--batch",
               static_cast<char*>(nullptr));
      _exit(127);
    }

    ::close(inPipe[0]);
    ::close(outPipe[1]);
    input = inPipe[1];
    output = ::fdopen(outPipe[0], "r");
    if (output == nullptr) throw std::runtime_error("cannot open cat-file output");
  }

  ~Batch() {
    if (input >= 0) ::close(input);
    if (output != nullptr) std::fclose(output);
    if (pid > 0) {
      int status = 0;
      while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
    }
  }

  std::string read(const std::string& sha) {
    writeAll(input, sha + "\n");

    char* line = nullptr;
    size_t capacity = 0;
    ssize_t length = ::getline(&line, &capacity, output);
    if (length < 0) {
      std::free(line);
      throw std::runtime_error("cat-file closed unexpectedly");
    }
    std::string header(line, static_cast<size_t>(length));
    std::free(line);
    header = trim(header);

    if (endsWith(header, " missing")) return {};

    // "<sha> <type> <size>"
    size_t firstSpace = header.find(' ');
    size_t secondSpace = header.find(' ', firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos) {
      throw std::runtime_error("unexpected cat-file header: " + header);
    }
    std::string type = header.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    size_t size = static_cast<size_t>(std::stoull(header.substr(secondSpace + 1)));

    std::string content(size, '\0');
    if (size > 0) {
      size_t got = std::fread(&content[0], 1, size, output);
      if (got != size) throw std::runtime_error("short read from cat-file");
    }
    std::fgetc(output);  // trailing newline
    if (type != "blob") return {};
    return content;
  }
};

Git::Git(std::string repo) {
  root_ = trim(runGit(repo, {"rev-parse", "--show-toplevel"}));
  if (root_.empty()) throw std::runtime_error("not a git repository: " + repo);
}

Git::~Git() = default;

std::string Git::head() const {
  return trim(runGit(root_, {"rev-parse", "HEAD"}));
}

std::string Git::resolve(const std::string& rev) const {
  return trim(runGit(root_, {"rev-parse", "--verify", rev + "^{commit}"}));
}

std::string Git::subject(const std::string& rev) const {
  return trim(runGit(root_, {"log", "-1", "--format=%s", rev}));
}

std::string Git::remoteName() const {
  try {
    std::string url = trim(runGit(root_, {"config", "--get", "remote.origin.url"}));
    if (!url.empty()) {
      size_t slash = url.find_last_of("/:");
      std::string name = slash == std::string::npos ? url : url.substr(slash + 1);
      if (endsWith(name, ".git")) name = name.substr(0, name.size() - 4);
      if (!name.empty()) return name;
    }
  } catch (const std::exception&) {
    // No remote configured; fall back to the directory name.
  }
  return baseName(root_);
}

std::vector<Commit> Git::log(int limit, int stride, const std::string& since) const {
  std::vector<std::string> args = {"rev-list", "--first-parent", "--reverse"};
  if (!since.empty()) args.push_back("--since=" + since);
  args.push_back("HEAD");

  std::string raw = runGit(root_, args);
  std::vector<std::string> shas;
  for (const auto& line : split(raw, '\n')) {
    std::string sha = trim(line);
    if (!sha.empty()) shas.push_back(sha);
  }
  if (shas.empty()) return {};

  std::vector<std::string> picked;
  if (limit > 0 && static_cast<int>(shas.size()) > limit) {
    if (limit == 1) {
      picked.push_back(shas.back());
    } else {
      for (int i = 0; i < limit; ++i) {
        size_t index = static_cast<size_t>(
            (static_cast<long long>(i) * (shas.size() - 1)) / (limit - 1));
        if (picked.empty() || picked.back() != shas[index]) {
          picked.push_back(shas[index]);
        }
      }
    }
  } else if (stride > 1) {
    for (size_t i = 0; i < shas.size(); i += static_cast<size_t>(stride)) {
      picked.push_back(shas[i]);
    }
    if (picked.back() != shas.back()) picked.push_back(shas.back());
  } else {
    picked = shas;
  }

  std::vector<Commit> commits;
  constexpr size_t kChunk = 400;
  for (size_t start = 0; start < picked.size(); start += kChunk) {
    size_t end = std::min(start + kChunk, picked.size());
    std::vector<std::string> logArgs = {
        "log", "--no-walk=unsorted", "--format=%H%x1f%ct%x1f%P%x1f%s%x1e"};
    for (size_t i = start; i < end; ++i) logArgs.push_back(picked[i]);

    std::string logRaw = runGit(root_, logArgs);
    std::vector<std::string> records = split(logRaw, '\x1e');
    for (const auto& record : records) {
      std::string r = record;
      while (!r.empty() && (r.front() == '\n' || r.front() == '\r')) r.erase(r.begin());
      if (r.empty()) continue;

      std::vector<std::string> fields = split(r, '\x1f');
      if (fields.size() < 4) continue;
      Commit commit;
      commit.sha = trim(fields[0]);
      commit.time = std::stoll(trim(fields[1]));
      for (const auto& parent : split(trim(fields[2]), ' ')) {
        if (!parent.empty()) commit.parents.push_back(parent);
      }
      commit.subject = fields[3];
      while (!commit.subject.empty() &&
             (commit.subject.back() == '\n' || commit.subject.back() == '\r')) {
        commit.subject.pop_back();
      }
      if (!commit.sha.empty()) commits.push_back(std::move(commit));
    }
  }

  return commits;
}

std::vector<TreeEntry> Git::lsTree(const std::string& sha) const {
  std::string raw = runGit(root_, {"ls-tree", "-r", "-z", sha});
  std::vector<TreeEntry> entries;
  for (const auto& record : split(raw, '\0')) {
    if (record.empty()) continue;
    size_t tab = record.find('\t');
    if (tab == std::string::npos) continue;
    std::string header = record.substr(0, tab);
    std::string path = record.substr(tab + 1);
    std::vector<std::string> parts = split(header, ' ');
    if (parts.size() < 3 || parts[1] != "blob") continue;
    entries.push_back({path, parts[2]});
  }
  return entries;
}

std::vector<std::string> Git::changedFiles(const std::string& sha) const {
  std::string raw;
  try {
    raw = runGit(root_, {"diff-tree", "--no-commit-id", "--name-only", "-r", "-z",
                         "-m", "--first-parent", sha});
  } catch (const std::exception&) {
    raw = runGit(root_, {"diff-tree", "--no-commit-id", "--name-only", "-r", "-z",
                         "--root", sha});
  }
  std::vector<std::string> files;
  for (const auto& record : split(raw, '\0')) {
    if (!record.empty()) files.push_back(record);
  }
  return files;
}

std::string Git::readBlob(const std::string& blob) const {
  if (!batch_) batch_ = std::make_unique<Batch>(root_);
  return batch_->read(blob);
}

}  // namespace strata
