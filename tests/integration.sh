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

echo "integration ok"
