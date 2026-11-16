#!/usr/bin/env bash
# End-to-end smoke test: build a tiny repository, ingest it, then ratchet a
# dependency cycle that appears in the second commit.
set -euo pipefail

BIN="${1:?usage: integration.sh /path/to/strata}"
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

git init -q "$TMP/repo"
cd "$TMP/repo"
git config user.email "test@example.com"
git config user.name "Test"

mkdir -p src/core src/api
cat > src/core/store.ts <<'EOF'
export const value = 1;
EOF
cat > src/api/handler.ts <<'EOF'
import { value } from "../core/store";
export const handler = () => value;
EOF
git add . && git commit -qm "core and api"

BASE="$(git rev-parse HEAD)"

cat > src/core/loop.ts <<'EOF'
import { handler } from "../api/handler";
export const loop = () => handler();
EOF
git add . && git commit -qm "introduce a cycle"

HEAD="$(git rev-parse HEAD)"

"$BIN" ingest . --db "$TMP/strata.db" > /dev/null 2>&1
"$BIN" diff "$BASE" "$HEAD" --repo . > "$TMP/diff.txt"
grep -q "src/core" "$TMP/diff.txt"
grep -q "src/api" "$TMP/diff.txt"

if "$BIN" check --repo . --baseline "$BASE" --target "$HEAD" > "$TMP/check.txt" 2>&1; then
  echo "expected a new cycle to fail the ratchet"
  cat "$TMP/check.txt"
  exit 1
fi
grep -qi "cycle" "$TMP/check.txt"
grep -q "0 -> 1" "$TMP/check.txt"

"$BIN" check --repo . --baseline "$HEAD" --target "$HEAD" > "$TMP/clean.txt" 2>&1
grep -q "no new architectural regressions" "$TMP/clean.txt"

"$BIN" check --repo . --baseline "$BASE" --target "$HEAD" --format json \
  > "$TMP/check.json" || true
grep -q '"kind": "cycle"' "$TMP/check.json"

"$BIN" check --repo . --baseline "$BASE" --target "$HEAD" --format markdown \
  > "$TMP/check.md" || true
grep -q "## strata check" "$TMP/check.md"
grep -q "src/api" "$TMP/check.md"

# C++ include directories: a unique suffix match connects cmd -> src.
git init -q "$TMP/cpp"
cd "$TMP/cpp"
git config user.email "test@example.com"
git config user.name "Test"
mkdir -p src cmd
cat > src/core.hpp <<'EOF'
#pragma once
inline int core() { return 1; }
EOF
git add . && git commit -qm "core"
CPP_BASE="$(git rev-parse HEAD)"
cat > cmd/main.cpp <<'EOF'
#include "core.hpp"
int main() { return core(); }
EOF
git add . && git commit -qm "cli"
CPP_HEAD="$(git rev-parse HEAD)"

"$BIN" diff "$CPP_BASE" "$CPP_HEAD" --repo . > "$TMP/cpp.txt"
grep -q "+ dependency  cmd -> src" "$TMP/cpp.txt"

# Rust: use crate::store::db should become a server -> store edge.
git init -q "$TMP/rust"
cd "$TMP/rust"
git config user.email "test@example.com"
git config user.name "Test"
mkdir -p src/store
cat > src/store/db.rs <<'EOF'
pub fn open() -> u8 { 1 }
EOF
git add . && git commit -qm "store"
RUST_BASE="$(git rev-parse HEAD)"
mkdir -p src/server
cat > src/server/mod.rs <<'EOF'
use crate::store::db;
pub fn boot() -> u8 { db::open() }
EOF
git add . && git commit -qm "server"
RUST_HEAD="$(git rev-parse HEAD)"

"$BIN" diff "$RUST_BASE" "$RUST_HEAD" --repo . > "$TMP/rust.txt"
grep -q "+ dependency  src/server -> src/store" "$TMP/rust.txt"

echo "integration ok"
