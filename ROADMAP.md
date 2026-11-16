# Roadmap

`strata` is pre-1.0. This is the work I plan to do, roughly in order. Items map
to GitHub issues; comment there before starting so we do not duplicate effort.

## v0.1 — the loop (done)

- [x] Ingest first-parent history with `--limit`, `--stride`, `--since`
- [x] Read trees through `git ls-tree` and a batched `git cat-file`
- [x] Lexical import extraction for Go, TypeScript/JavaScript, Python, C/C++,
      Rust, and Java/Kotlin
- [x] Import resolution to directories, with weighted edges
- [x] Cumulative churn per module
- [x] Tarjan strongly connected components for cycles
- [x] SQLite snapshot store with a repo → snapshot → node/edge model
- [x] Layer rules and allow-lists in `.strata.json`
- [x] `strata check` ratchet, with text, JSON, and Markdown output
- [x] `strata diff` between arbitrary revisions
- [x] Embedded canvas UI: timeline, play/pause, force layout, zoom/pan, node
      inspector, layer colours, "new since previous" flashes
- [x] Single-binary distribution (web assets compiled in)
- [x] Unit tests, an end-to-end test, and CI on Linux and macOS
- [x] A generated demo repository with a deliberate cycle

## v0.2 — resolution fidelity

Better answers on real repositories, without adding a compiler dependency.

- [ ] TypeScript: `tsconfig` `baseUrl` and `paths`, project references,
      workspace package names
- [ ] JavaScript: resolve `exports`/`imports` maps from `package.json`
- [ ] Go: build tags, multi-module workspaces, `internal/` visibility rules
- [ ] Python: `src/` layouts, namespace packages, `pyproject.toml` packages
- [ ] C/C++: include directories from `compile_commands.json`
- [ ] Rust: workspace crates and `#[path]` modules
- [ ] Java/Kotlin: source roots and package-to-directory mapping
- [ ] Node granularity: `--granularity file|dir|depth:N`, with automatic
      file-level nodes for flat directories
- [ ] Collapse/expand clusters in the UI; focus a module and its neighbours
- [ ] Per-language edge styles and a legend

## v0.3 — the product

- [ ] A/B mode in the UI: pin one frame, diff the current frame against it
- [ ] `strata export --format svg|json|graphml` for docs and other tools
- [ ] `strata check --baseline-file .strata/baseline.json` for shallow CI
      clones and monorepos where the baseline is not an ancestor
- [ ] `--format sarif` for code-scanning dashboards
- [ ] A GitHub Action that posts the Markdown report as a pull request comment
- [ ] Saved views and shareable permalinks (`?sha=…&focus=…`)
- [ ] A multi-repository landing page for teams that want one dashboard
- [ ] A `strata graph` subcommand that dumps one frame as JSON

## v0.4 — scale

- [ ] Incremental ingest: content-addressed blob cache, reparse only changed
      files, append new snapshots instead of replacing the repository
- [ ] Parallel parsing with a bounded worker pool
- [ ] Benchmark: 10,000 commits × 100,000 files in under a minute
- [ ] Server-side layout and level-of-detail for graphs above ~1,000 nodes
- [ ] Optional PostgreSQL backend for shared instances
- [ ] Streaming API for very large histories

## Quality track

- [ ] Property tests for the glob matcher and Tarjan implementation
- [ ] Fuzz the import scanners with arbitrary bytes
- [ ] ASan/UBSan job in CI
- [ ] Mutation testing to prove the suite catches regressions
- [ ] Golden-file fixtures for every language extractor
- [ ] Benchmarks for ingest and the graph API
- [ ] Windows support: replace the POSIX pipe layer in `src/git.cpp`
- [ ] Shell completions and a man page

## Good first issues

These are intentionally small and self-contained:

- Add `.mts` and `.cts` to the TypeScript extension list, with a test
- Add a `--port` alias for `--addr 127.0.0.1:<port>`
- Print the elapsed time and snapshot count in `strata serve`
- Add `strata ingest --dry-run` that prints the sampled commits
- Add a `--collapse` flag that maps every node to its top-level directory
- Show the commit date in the timeline tooltip
- Add a keyboard shortcut overlay link in the footer
- Improve the empty state when a repository has no source files
- Add a `?` tooltip explaining the red cycle edges
- Document `DESTDIR` for `make install` and add shell completions
