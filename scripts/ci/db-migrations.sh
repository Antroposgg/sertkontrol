#!/usr/bin/env bash
# Проверка миграций на чистом PostgreSQL 16: применить дважды (идемпотентность) + инварианты схемы.
set -euo pipefail
cd "$(dirname "$0")/../.."
name="sk-migrations-test-$$"
cleanup() { docker rm -f "$name" >/dev/null 2>&1 || true; }
trap cleanup EXIT
docker run -d --name "$name" -e POSTGRES_HOST_AUTH_METHOD=trust -e POSTGRES_DB=sertkontrol \
  -e POSTGRES_USER=sertkontrol postgres:16 >/dev/null
for _ in $(seq 1 60); do
  docker exec "$name" pg_isready -U sertkontrol -d sertkontrol -h 127.0.0.1 >/dev/null 2>&1 && break
  sleep 1
done
docker cp db "$name:/db"
docker cp tests/db/schema_test.sql "$name:/schema_test.sql"
run() { docker exec -e PGHOST=127.0.0.1 -e PGUSER=sertkontrol -e PGDATABASE=sertkontrol "$name" "$@"; }
run sh /db/migrate.sh
run sh /db/migrate.sh | tee /dev/stderr | grep -q "уже применена"
run psql -v ON_ERROR_STOP=1 -q -f /schema_test.sql
echo "db-migrations: OK"
