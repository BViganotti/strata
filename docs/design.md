# Design notes

These notes describe the model behind `strata` and the trade-offs in version
0.1. They are intentionally concrete so the next change has something to argue
with.

## Goals

1. **History is the product.** The interesting question is not only "what is the
   graph?" but "when did it become this?" Every commit is a first-class frame.
2. **No setup, no services.** One binary, one SQLite file, localhost only. The
   UI is compiled in.
3. **Honest heuristics.** Import resolution is lexical. When it cannot resolve a
   specifier it drops the edge instead of guessing; the limitations are
   documented rather than hidden behind a "language server required" wall.
4. **A ratchet, not a gate.** Legacy repositories already have cycles. The CI
   mode compares against a baseline and fails only on new debt.

## Non-goals

- Replacing a compiler, an LSP, or a build system.
- Precise call graphs, type resolution, or dead-code analysis.
- Multi-user hosting, authentication, or a SaaS.
- Reading author identities. `strata` deliberately does not store them.

## Data model

```
repo(id, path, name, head, ingested_at)
snapshot(id, repo_id, seq, sha, time, subject, parents,
         nodes, edges, cycles, loc)
node(snapshot_id, path, loc, files, churn, lang)
edge(snapshot_id, src, dst, weight)
```

- `seq` is the position in the ingested timeline (0-based), not the commit's
  position in the full history. Sampling compresses it.
- A node is a **directory** that contains at least one recognised source file.
  The repository root is `.`.
- `loc` counts lines in recognised source files, `files` counts those files, and
  `churn` is the number of ingested commits that touched the directory.
- An edge is `src -> dst` with `weight` equal to the number of import statements
  from files in `src` to files in `dst`. Self-edges are dropped.
- Cycles are not stored; they are recomputed from the edges with Tarjan's
  algorithm on read. This keeps the schema small and the algorithm testable.

Snapshots are written in one transaction per commit. Re-ingesting a repository
clears its snapshots and rebuilds them; the schema is otherwise append-only.

## Ingest pipeline

1. `git rev-list --first-parent --reverse [--since=…] HEAD` produces the
   timeline. `--limit` samples it evenly while preserving the first and last
   commits; `--stride` is the coarser, explicit version.
2. One `git log --no-walk=unsorted` call fetches metadata for the sampled
   commits, so ingesting 200 frames does not spawn 200 `git show` processes.
3. `git ls-tree -r -z <sha>` lists the tree. Every blob is available to the
   resolver (not just source files) so `index.ts`, `__init__.py`, and headers
   can be found.
4. A single `git cat-file --batch` process reads blobs. The reader parses the
   `<sha> <type> <size>` header, reads exactly `size` bytes, and consumes the
   trailing newline; non-blob objects are skipped.
5. Each file runs through a lexical extractor. Files are filtered by the
   default and configured ignore globs before they are read.
6. The resolver maps specifiers to directories using the snapshot's own file
   set and, for Go, the module path from `go.mod`.
7. Churn is updated from `git diff-tree` before the graph is built, so a frame's
   churn includes its own changes.

## Import grammar

The extractors are intentionally small scans, not parsers:

- **Go**: `import "x"` plus `import ( … )` blocks. Aliases (`_`, `.`) are
  ignored; the quoted specifier is what matters.
- **TypeScript/JavaScript**: a quoted string preceded by `from`, `import`,
  `require(`, or `import(`. This catches side-effect imports and dynamic
  imports. It does not evaluate template literals.
- **Python**: `from a.b import c` and `import a, b` lines, including leading
  dots for relative imports. Multi-line backslash continuations are handled at
  the line level.
- **C/C++**: `#include "local.h"` only. Angle-bracket includes are treated as
  system headers and ignored.
- **Rust**: `mod name;`, `use crate::…`, `use self::…`, `use super::…`.
- **Java/Kotlin**: `import` statements, including `static` imports.

The resolver is deliberately conservative: a relative TypeScript specifier must
match a real file (with a known extension or `index.*`), a Go import must start
with the module path, and an unresolved specifier produces no edge.

## Ratchet semantics

A violation has a stable `key`:

- **cycle**: `cycle:` plus the sorted, `|`-separated member list;
- **layer**: `layer:` plus `from->to`.

`strata check` computes violations for the baseline and the target, then reports
target violations whose key is absent from the baseline. This has two useful
consequences:

- existing debt is ignored without a suppression file;
- if a cycle changes membership, it is a *new* cycle and fails — the right
  behaviour, because the cycle was restructured.

`max_cycles` is a separate absolute gate. It defaults to `-1` (off) so the
ratchet works on a repository with existing cycles. Layer rules only apply when
both endpoints of an edge are in a layer, and same-layer dependencies are always
allowed.

## API

`GET /api/meta` returns the repository, the layer config, and one entry per
snapshot with counts, time, and subject.

`GET /api/graph?sha=<prefix>` returns nodes and edges for one frame, plus:

- `cycle` flags on nodes and edges;
- `added` flags relative to the previous frame;
- `since`, the first sequence number in which a node or edge appeared;
- the cycle list and the current violations;
- a small diff summary (added/removed modules and edges).

Everything is computed from SQLite at request time. There is no cache and no
background worker; a frame is small enough that this is the simplest correct
thing. Graphs are cached client-side (bounded to 60 frames).

## UI

The canvas UI is plain JavaScript with no build step. A force simulation runs
synchronously for ~280 ticks when a frame is applied; it is seeded from the
previous frame's positions so scrubbing does not reshuffle the layout. New
modules start near an existing neighbour. Reciprocal edges are drawn as slight
quadratic curves so both directions of a cycle are visible.

The camera fits the graph once, accounting for the inspector panel, and stays
put across frames so the user can watch a cluster move rather than chase it.

## Privacy and safety

- No author identities are read or stored.
- The server binds to `127.0.0.1` by default; `--addr 0.0.0.0:…` is explicit.
- The API is read-only. There is no endpoint that writes to the repository,
  executes code, or shells out with user input.
- Git commands are executed with an explicit argument vector (`fork`/`execvp`),
  never through a shell string, so repository paths and refs cannot be
  interpreted as shell syntax.
- The static asset server rejects paths containing `..`.

## Alternatives considered

- **libgit2**: would remove the external `git` process, but adds a large C
  dependency and a second source of truth for worktrees, alternates, and
  shallow clones. Shelling out keeps `strata` compatible with whatever git the
  user already trusts.
- **tree-sitter**: would give precise ASTs for seven languages, but adds native
  build complexity and a grammar per language. The lexical pass is fast,
  dependency-free, and honest about being a heuristic; precise resolution can
  be added incrementally where it pays off.
- **Graphviz / server-side layout**: produces prettier static diagrams, but the
  timeline needs an interactive layout that preserves positions across frames.
  A small force simulation in the browser is the right trade-off at this scale.
- **A hosted service**: rejected. The repository never leaves the machine.
