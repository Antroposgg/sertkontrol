#!/usr/bin/env bash
# Выполняет команду с временным PostgreSQL 16 (миграции применены) и строкой подключения в SK_TEST_PG.
# Если SK_TEST_PG уже задан — использует его. Без Docker тесты с БД пропускаются (GTEST_SKIP) с предупреждением,
# а при SK_REQUIRE_PG=1 — ошибка.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ -n "${SK_TEST_PG:-}" ]]; then
  exec "$@"
fi
if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
  # SK_REQUIRE_PG=1 (ворота): пропуск тестов с БД — ошибка, а не предупреждение.
  if [[ "${SK_REQUIRE_PG:-0}" == 1 ]]; then
    echo "::error::Docker недоступен и SK_TEST_PG не задан, а SK_REQUIRE_PG=1" >&2
    exit 1
  fi
  echo "::warning::Docker недоступен — тесты с PostgreSQL будут пропущены" >&2
  exec "$@"
fi
name="sk-test-pg-$$"
cleanup() { docker rm -f "$name" >/dev/null 2>&1 || true; }
trap cleanup EXIT
docker run -d --name "$name" -p 127.0.0.1::5432 -e POSTGRES_HOST_AUTH_METHOD=trust \
  -e POSTGRES_DB=sertkontrol -e POSTGRES_USER=sertkontrol postgres:16 >/dev/null
for _ in $(seq 1 60); do
  docker exec "$name" pg_isready -U sertkontrol -d sertkontrol -h 127.0.0.1 >/dev/null 2>&1 && break
  sleep 1
done
docker cp "$root/db" "$name:/db"
docker exec -e PGHOST=127.0.0.1 -e PGUSER=sertkontrol -e PGDATABASE=sertkontrol "$name" sh /db/migrate.sh >/dev/null
port="$(docker port "$name" 5432/tcp | head -1 | sed 's/.*://')"
export SK_TEST_PG="host=127.0.0.1 port=${port} dbname=sertkontrol user=sertkontrol"
echo "with-pg: PostgreSQL 16 на 127.0.0.1:${port}" >&2
"$@"
