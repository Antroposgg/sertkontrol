# tests — модульные и интеграционные тесты

| Каталог | Бинарь / скрипт | Что проверяет |
|---|---|---|
| `contracts/` | `contracts_test` | `Result`, коды ошибок и статусы, все fake C1/C2/C4/C5 |
| `canon/` + `canon_golden.tsv` | `canon_test` | Golden-файл C1 (первые 3 строки — АРХ §7.1), грамматика, `find_numbers` |
| `snapshot/` | `snapshot_test` | Формат v1: roundtrip, детерминированность, 13 видов повреждений, holder |
| `verify/` | `verify_test` | Табличный тест правил, каталог `docs/rules.md`, ИНН, представления, нечёткий поиск и property «кандидаты ≡ перебор» |
| `recog/` | `recog_test` | F2 на `fixtures/pdf/` (исходники `.tex`), лимиты |
| `maxapi/` | `maxapi_test`, `maxapi_http_test` | initData, события (`fixtures/max/`), C9, лимиты MAX, HTTP-клиент против локального сервера |
| `ingest/` | `ingest_test` | CLI, нормализация, TSV-адаптер, сборка `data/demo`, quality gate, версии в PostgreSQL |
| `certd/` | `certd_test`, `certd_pg_test`, `openapi_test` | Конфиг, REST, бот, webhook, компоненты; домен, IDOR, outbox на PostgreSQL; `openapi.yaml` ≡ маршруты |
| `concurrency/` | `concurrency_test` (метка `tsan`) | Замена снапшота под нагрузкой читателей, лимитер проверок под конкуренцией — под ThreadSanitizer (пресет `clang-tsan`) |
| `db/` | `scripts/ci/db-migrations.sh` | Миграции и инварианты схемы на PostgreSQL 16 |
| `support/` | — | `checked`, `files`, `pg` (`SK_TEST_PG`), `init_data` (подпись initData через OpenSSL) |

Тесты с БД запускаются через `scripts/ci/with-pg.sh` (временный PostgreSQL 16 в Docker); без `SK_TEST_PG` они
пропускаются с сообщением. `cpp-build-test.sh` вызывает `with-pg.sh` сам.

Fuzz-цели — [`fuzz/`](../fuzz/README.md), бенчмарки — [`bench/`](../bench/README.md); контракт C7 на живом стеке —
`scripts/ci/contract-test.sh`.

Новый тест: файл в подкаталоге модуля + `sk_add_test(...)` в `tests/CMakeLists.txt`. Тесты гоняются дважды (`ctest` пресеты с `repeat until-fail:2`) — флаки не допускаются.
