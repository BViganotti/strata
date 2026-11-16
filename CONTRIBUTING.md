# Contributing

Thanks for taking the time to look at `strata`. This is a small, opinionated
project; the notes below keep it easy to review.

## Build and test

```console
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
$ cmake --build build -j
$ ctest --test-dir build --output-on-failure
```

Requirements are a C++17 compiler, CMake 3.16+, `git`, and SQLite 3 development
headers. On macOS the SDK provides SQLite; on Debian/Ubuntu install
`libsqlite3-dev`.

For UI iteration, serve the files from disk instead of rebuilding:

```console
$ ./build/strata serve --web-dir web --db /tmp/demo.db
```

## Code layout

| Path | Responsibility |
| --- | --- |
| `src/git.cpp` | Thin git plumbing: rev-list, ls-tree, diff-tree, batched cat-file |
| `src/lang.cpp` | Lexical import extraction per language |
| `src/analyze.cpp` | Resolve imports and build one commit's graph |
| `src/graph.cpp` | Graph model, Tarjan cycles, diffs, path helpers |
| `src/config.cpp` | JSONC config and the glob matcher |
| `src/store.cpp` | SQLite schema and queries |
| `src/check.cpp` | Layer and cycle violations, ratchet comparison |
| `src/web.cpp` | HTTP server, JSON API, static assets |
| `web/` | Vanilla HTML/CSS/JS canvas UI, compiled into the binary |
| `tests/` | Unit tests and the end-to-end repository test |

Two headers are vendored in `third_party/` (MIT): `cpp-httplib` and
`nlohmann/json`. Do not add a package manager dependency without discussing it
first; the single-binary, offline build is a feature.

## Style

- C++17, two-space indentation, `snake_case` for variables and functions,
  `PascalCase` for types. Match the surrounding code.
- Prefer the standard library; avoid `std::regex` in hot paths (the parsers use
  small hand-written scans on purpose).
- Keep the CLI thin. Parsing and graph logic belong in `src/`.
- Comments explain *why*, not *what*. A non-obvious heuristic deserves a note.
- No exceptions across the C API boundary; the CLI catches at the top level.

## Tests

Every change to parsing, resolution, the glob matcher, or the ratchet needs a
test. Unit tests live in `tests/unit.cpp`; the integration script builds a real
temporary repository and must keep passing. Run the full suite before opening a
pull request.

When adding a language, add:

1. a `Lang` value and extension mapping in `src/lang.cpp`;
2. an extractor with a focused unit test;
3. a resolver case in `src/analyze.cpp`;
4. a row in the README's supported-language table.

## Data and privacy

- Use synthetic repositories in tests. Never check in a real repository, a
  customer database, or anything containing personal data.
- Do not add author names or email addresses to the schema or the UI.
- Never commit credentials, tokens, or private keys. Database files (`*.db`) and
  build output are ignored.
- If you find a secret in the history, stop and report it privately; do not
  rewrite shared history without coordinating.

## Commits and pull requests

- Write imperative commit subjects, scoped where useful:
  `feat: resolve tsconfig path aliases`, `fix: handle a missing go.mod`.
- Keep commits focused. A refactor and a behaviour change belong in separate
  commits.
- Describe the user-visible change, the test you added, and any trade-off in the
  pull request.
- Small is beautiful. If a change needs a new dependency, a schema migration,
  or a change to the ratchet semantics, open an issue first.

## Reporting bugs

Include the output of `strata version`, the OS, the repository shape (language,
approximate size), the exact commands, and the expected vs. actual result. A
minimal synthetic repository is the fastest way to get a fix.
