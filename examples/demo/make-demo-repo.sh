#!/usr/bin/env bash
# Builds a small, realistic service whose architecture changes across five
# commits: a monolith, a layered split, a cycle, a plugin, and a fix.
#
# Usage: make-demo-repo.sh [target-dir]
set -euo pipefail

TARGET="${1:-strata-demo}"
if [ -e "$TARGET" ]; then
  echo "refusing to overwrite $TARGET" >&2
  exit 1
fi

git init -q -b main "$TARGET"
cd "$TARGET"
git config user.name "strata demo"
git config user.email "demo@example.com"

cat > .strata.json <<'EOF'
{
  // Layer rules for the demo. core may not depend on api.
  "layers": [
    {"name": "shared",   "paths": ["packages/shared/**"]},
    {"name": "core",     "paths": ["src/core/**"]},
    {"name": "api",      "paths": ["src/api/**"]},
    {"name": "cli",      "paths": ["src/cli/**"]},
    {"name": "plugins",  "paths": ["src/plugins/**"]},
    {"name": "adapters", "paths": ["src/adapters/**"]},
    {"name": "worker",   "paths": ["apps/worker/**"]}
  ],
  "allow": {
    "shared": [],
    "core": ["shared"],
    "api": ["core", "shared"],
    "cli": ["core", "api", "shared"],
    "plugins": ["core", "shared"],
    "adapters": ["shared"],
    "worker": ["core", "api", "adapters", "shared"]
  },
  "ignore": ["**/*.test.ts"]
}
EOF

cat > README.md <<'EOF'
# ledger demo

A tiny invoice ledger used to exercise strata. It is intentionally shaped so
that one commit introduces a dependency cycle and the next one removes it.
EOF

mkdir -p src
cat > src/main.ts <<'EOF'
import { startServer } from "./http";
import { openStore } from "./store";

const store = openStore("ledger.db");
startServer(store, 8080);
EOF

cat > src/http.ts <<'EOF'
import type { Store } from "./store";

export function startServer(store: Store, port: number): void {
  console.log(`listening on ${port} with ${store.size()} entries`);
}
EOF

cat > src/store.ts <<'EOF'
export interface Store {
  size(): number;
  get(key: string): string | undefined;
}

export function openStore(path: string): Store {
  const rows = new Map<string, string>();
  void path;
  return {
    size: () => rows.size,
    get: (key) => rows.get(key),
  };
}
EOF

git add -A
git commit -qm "feat: single-file ledger service"

mkdir -p src/core src/api src/cli src/adapters src/plugins packages/shared apps/worker
git rm -q src/main.ts src/http.ts src/store.ts

cat > packages/shared/types.ts <<'EOF'
export interface Account {
  id: string;
  balanceCents: number;
}

export function empty(id: string): Account {
  return { id, balanceCents: 0 };
}
EOF

cat > packages/shared/config.ts <<'EOF'
export const config = {
  currency: "USD",
  pageSize: 50,
};
EOF

cat > src/adapters/postgres.ts <<'EOF'
import { empty, type Account } from "../../packages/shared/types";

export function loadAccount(id: string): Account {
  return empty(id);
}
EOF

cat > src/adapters/redis.ts <<'EOF'
import { config } from "../../packages/shared/config";

export function cacheKey(id: string): string {
  return `${config.currency}:account:${id}`;
}
EOF

cat > src/core/money.ts <<'EOF'
export function cents(amount: string): number {
  return Math.round(Number.parseFloat(amount) * 100);
}

export function format(value: number): string {
  return (value / 100).toFixed(2);
}
EOF

cat > src/core/store.ts <<'EOF'
import type { Account } from "../../packages/shared/types";
import { empty } from "../../packages/shared/types";

export interface Store {
  get(key: string): Account;
  put(key: string, value: Account): void;
  size(): number;
}

export function openStore(path: string): Store {
  const rows = new Map<string, Account>();
  void path;
  return {
    get: (key) => rows.get(key) ?? empty(key),
    put: (key, value) => rows.set(key, value),
    size: () => rows.size,
  };
}
EOF

cat > src/core/ledger.ts <<'EOF'
import { config } from "../../packages/shared/config";
import { cents } from "./money";
import type { Store } from "./store";

export class Ledger {
  constructor(private readonly store: Store) {}

  record(account: string, amount: string): number {
    const current = this.store.get(account);
    const next = current.balanceCents + cents(amount);
    this.store.put(account, { ...current, balanceCents: next });
    return next * config.pageSize;
  }
}
EOF

cat > src/api/http.ts <<'EOF'
import type { Store } from "../core/store";

export function get(store: Store, key: string): string {
  return JSON.stringify(store.get(key));
}
EOF

cat > src/api/routes.ts <<'EOF'
import { Ledger } from "../core/ledger";
import type { Store } from "../core/store";
import { get } from "./http";

export function routes(store: Store): Record<string, (key: string) => unknown> {
  const ledger = new Ledger(store);
  return {
    balance: (key) => get(store, key),
    record: (key) => ledger.record(key, "10.00"),
  };
}
EOF

cat > src/api/handlers.ts <<'EOF'
import { config } from "../../packages/shared/config";
import { routes } from "./routes";
import type { Store } from "../core/store";

export function handlers(store: Store): unknown {
  return { currency: config.currency, table: routes(store) };
}
EOF

cat > src/cli/main.ts <<'EOF'
import { handlers } from "../api/handlers";
import { openStore } from "../core/store";

const store = openStore("ledger.db");
console.log(JSON.stringify(handlers(store)));
EOF

cat > apps/worker/worker.ts <<'EOF'
import { loadAccount } from "../../src/adapters/postgres";
import { Ledger } from "../../src/core/ledger";
import { openStore } from "../../src/core/store";

export function run(id: string): number {
  const ledger = new Ledger(openStore("worker.db"));
  return ledger.record(loadAccount(id).id, "1.00");
}
EOF

cat > src/plugins/csv.ts <<'EOF'
import { Ledger } from "../core/ledger";
import { openStore } from "../core/store";

export function exportCsv(): string {
  const ledger = new Ledger(openStore("ledger.db"));
  return String(ledger.record("csv", "1.00"));
}
EOF

git add -A
git commit -qm "refactor: split shared, core, api, cli, adapters and the worker"

cat > src/core/report.ts <<'EOF'
import { get } from "../api/http";
import type { Store } from "./store";

export function report(store: Store, account: string): string {
  return get(store, account);
}
EOF

git add -A
git commit -qm "feat: monthly report reads through the api"

cat > src/plugins/json.ts <<'EOF'
import { report } from "../core/report";
import { openStore } from "../core/store";

export function exportJson(account: string): string {
  return report(openStore("ledger.db"), account);
}
EOF

git add -A
git commit -qm "feat: json export plugin"

cat > src/core/client.ts <<'EOF'
export function read(path: string): string {
  return `value:${path}`;
}
EOF

cat > src/core/report.ts <<'EOF'
import { read } from "./client";
import type { Store } from "./store";

export function report(store: Store, account: string): string {
  return read(`/report/${store.get(account).id}`);
}
EOF

git add -A
git commit -qm "fix: break the core -> api cycle"

echo "created $TARGET with $(git rev-list --count HEAD) commits"
echo
echo "  strata ingest $TARGET --db $TARGET.db"
echo "  strata check --repo $TARGET --baseline HEAD~3 --target HEAD~2"
echo "  strata serve  --db $TARGET.db"
