#!/bin/sh
# Идемпотентный мигратор: применяет db/migrations/NNNN_*.sql по порядку, каждую — в своей
# транзакции вместе с записью в schema_migrations. Повторный запуск ничего не делает.
# Подключение — стандартные переменные libpq: PGHOST, PGPORT, PGDATABASE, PGUSER, PGPASSWORD.
set -eu
export PGOPTIONS="${PGOPTIONS:-} -c client_min_messages=warning"
dir="${MIGRATIONS_DIR:-$(dirname "$0")/migrations}"

psql -v ON_ERROR_STOP=1 -q <<'SQL'
CREATE TABLE IF NOT EXISTS schema_migrations (
    name        text        PRIMARY KEY,
    applied_at  timestamptz NOT NULL DEFAULT now()
);
SQL

for file in "$dir"/[0-9][0-9][0-9][0-9]_*.sql; do
  name="$(basename "$file")"
  applied="$(psql -v ON_ERROR_STOP=1 -tAq -v name="$name" <<'SQL'
SELECT count(*) FROM schema_migrations WHERE name = :'name';
SQL
)"
  if [ "$applied" = "1" ]; then
    echo "migrate: $name — уже применена"
    continue
  fi
  echo "migrate: применяю $name"
  { cat "$file"; printf "\nINSERT INTO schema_migrations (name) VALUES (:'name');\n"; } |
    psql -v ON_ERROR_STOP=1 -q --single-transaction -v name="$name"
done
echo "migrate: готово"
