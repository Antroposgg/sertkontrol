# tests — модульные и интеграционные тесты

| Каталог | Бинарь / скрипт | Что проверяет |
|---|---|---|
| `contracts/` | `contracts_test` | `Result`, коды ошибок и статусы, все fake C1/C2/C4/C5 |
| `certd/` | `certd_test` | конфиг, `/healthz`, `FakeDomainService` |
| `ingest/` | `ingest_test` | CLI, `FakeSource` (C3) |
| `db/` | `scripts/ci/db-migrations.sh` | миграции и инварианты схемы на PostgreSQL 16 |
| `support/` | — | `sk::test::checked` — проверенный доступ к `optional` |

Новый тест: файл в подкаталоге модуля + `sk_add_test(...)` в `tests/CMakeLists.txt`. Тесты гоняются дважды (`ctest` пресеты с `repeat until-fail:2`) — флаки не допускаются.
