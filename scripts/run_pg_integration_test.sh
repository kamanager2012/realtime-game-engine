#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SCHEMA="${ROOT}/deploy/initdb/01_schema.sql"
DEFAULT_URL="postgresql://poker:${DB_PASSWORD:-poker}@localhost:5432/poker"

pg_url_ready() {
  local url="$1"
  if command -v psql >/dev/null 2>&1; then
    psql "$url" -c 'SELECT 1' >/dev/null 2>&1
    return $?
  fi
  pg_isready -h "${POKER_PG_HOST:-localhost}" -p "${POKER_PG_PORT:-5432}" -q 2>/dev/null
}

apply_schema() {
  local url="$1"
  if [[ ! -f "$SCHEMA" ]]; then
    echo "schema not found: $SCHEMA" >&2
    exit 1
  fi
  if ! command -v psql >/dev/null 2>&1; then
    echo "psql not found; ensure schema is applied: $SCHEMA" >&2
    return 0
  fi
  echo "Applying schema: $SCHEMA"
  psql "$url" -v ON_ERROR_STOP=1 -f "$SCHEMA"
}

wait_for_postgres() {
  local url="$1"
  local attempts=30
  local i
  for i in $(seq 1 "$attempts"); do
    if pg_url_ready "$url"; then
      return 0
    fi
    sleep 1
  done
  return 1
}

ensure_postgres() {
  if [[ -n "${POKER_POSTGRES_URL:-}" ]]; then
    if pg_url_ready "$POKER_POSTGRES_URL"; then
      echo "Using POKER_POSTGRES_URL from environment"
      apply_schema "$POKER_POSTGRES_URL"
      export POKER_POSTGRES_URL
      return 0
    fi
    echo "POKER_POSTGRES_URL is set but unreachable: $POKER_POSTGRES_URL" >&2
  fi

  export POKER_POSTGRES_URL="${POKER_POSTGRES_URL:-$DEFAULT_URL}"
  if pg_url_ready "$POKER_POSTGRES_URL"; then
    echo "Using local Postgres: $POKER_POSTGRES_URL"
    apply_schema "$POKER_POSTGRES_URL"
    return 0
  fi

  if ! command -v docker >/dev/null 2>&1; then
    cat >&2 <<EOF
PostgreSQL is not reachable.

Options:
  1. Install and start PostgreSQL locally, then re-run this script
  2. Export POKER_POSTGRES_URL to an existing instance
  3. Install Docker and ensure registry access for postgres:16-alpine
EOF
    exit 1
  fi

  echo "Local Postgres unavailable; starting via docker compose..."
  export DB_PASSWORD="${DB_PASSWORD:-poker}"
  if ! docker compose -f deploy/docker-compose.yml up -d postgres; then
    cat >&2 <<EOF
Failed to start Postgres container (image pull or compose error).

Use a local PostgreSQL instance instead:
  sudo apt install postgresql-18
  export POKER_POSTGRES_URL='postgresql://USER:PASS@localhost:5432/poker'
  psql "\$POKER_POSTGRES_URL" -f deploy/initdb/01_schema.sql
  ./scripts/run_pg_integration_test.sh
EOF
    exit 1
  fi

  if ! wait_for_postgres "$POKER_POSTGRES_URL"; then
    echo "Postgres container did not become ready on localhost:5432" >&2
    exit 1
  fi
  echo "Docker Postgres ready: $POKER_POSTGRES_URL"
}

ensure_postgres

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target poker_tests -j"$(nproc)"

export POKER_POSTGRES_URL
./build/tests/poker_tests --gtest_filter='PostgresMirrorIntegrationTest.*'
