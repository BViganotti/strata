# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[semantic versioning](https://semver.org/).

## [0.1.0] - 2026-11-16

The first public release. The core loop works end to end on Linux and macOS.

### Added

- Ingest first-parent git history, with `--limit`, `--stride`, `--since`, and
  `--name` controls.
- Batched blob reads through `git cat-file --batch`; no process per file.
- Lexical import extraction for Go, TypeScript/JavaScript, Python, C/C++, Rust,
  and Java/Kotlin.
- Directory-level dependency graphs with weighted edges, line counts, file
  counts, and cumulative churn.
- Unique-suffix fallback for C/C++ includes that rely on include directories.
- Tarjan strongly connected components for cycle detection.
- SQLite snapshot store (`repo` → `snapshot` → `node`/`edge`).
- Layer rules and allow-lists in `.strata.json` (JSONC), with same-layer
  dependencies allowed and glob ignores.
- `strata check` ratchet: text, JSON, and Markdown output; exit codes for CI.
- `strata diff` between arbitrary revisions.
- Embedded canvas UI with a timeline, play/pause, force layout, zoom/pan,
  inspector, layer colours, cycle highlighting, and "new since previous"
  flashes.
- Single-binary distribution; the web assets are compiled in.
- Unit tests and an end-to-end test that builds a temporary repository,
  introduces a cycle, and asserts the ratchet catches it.
- CI on Ubuntu and macOS.
- A generated five-commit demo repository and two screenshots.

### Known limitations

- Nodes are directories; flat `src/` layouts collapse.
- Import resolution is heuristic and does not read build systems yet.
- History is first-parent only.
- POSIX only.
- One repository per database.

See [ROADMAP.md](ROADMAP.md) for the next releases.

[0.1.0]: https://github.com/BViganotti/strata/releases/tag/v0.1.0
