# db — схема PostgreSQL и мигратор (владелец R3; C8 — R1, C9 — R3/R4)

**Статус:** этап 2 — все 11 таблиц АРХ §6; демо-пара N/N+1 (`snapshot_version.demo_stage`), ошибки задач (`job.last_error`), индексы истории и `notify_changes`.

## Ключевые файлы
| Файл | Что внутри |
|---|---|
| `migrations/0001_init.sql` | `app_user`, `supplier`, `portfolio_item`, `snapshot_version`, `registry_change`, `notification`, `inbound_update`, `outbox`, `job`, `dialog_state`, `check_log` |
| `migrations/0002_check_log_batch.sql` | `check_log.batch_id` — кнопка «Поставить все» |
| `migrations/0003_demo_pair_and_jobs.sql` | `snapshot_version.demo_stage` (0 — N, 1 — N+1, только у демо), `job.last_error`, индексы `registry_change(doc_key)` и `portfolio_item(last_version)` |
| `migrate.sh` | Идемпотентный мигратор (POSIX sh + psql): таблица `schema_migrations`, каждая миграция — в своей транзакции |

## Как применяется
Сервис `migrate` в `compose.yaml` (образ `postgres:16`) запускается после `healthy` PostgreSQL; `certd` и `ingest` стартуют после его успешного завершения.

## Куда добавлять
Новая миграция — новый файл `NNNN_<суть>.sql` со следующим номером. Применённые миграции не редактируются. Изменение C8/C9 — запись в docs/changelog-contracts.md.

## Инварианты
- Реестр в БД не хранится (ADR-0001).
- `UNIQUE NULLS NOT DISTINCT (user_id, doc_key, sku)` — два `sku IS NULL` считаются дубликатом (PG 15+).
- `UNIQUE (portfolio_item_id, version, kind)` — идемпотентный fan-out уведомлений; `inbound_update.dedup_key` — дедупликация webhook (АРХ §7.5).
- `demo_stage` допустим только у демо-версий (CHECK); certd держит последние `ready` каждой роли: боевую, демо N и демо N+1.
- `NOTIFY snapshot_ready, '<version>'` — после COMMIT транзакции, в которой версия стала `ready` (C8).
- Все запросы из кода — только параметризованные (`$1`), с фильтром по `user_id` (АРХ §10).

## Тесты
```bash
scripts/ci/db-migrations.sh
```
Чистый PG 16 → миграции дважды (второй раз — «уже применена») → `tests/db/schema_test.sql` (NULLS NOT DISTINCT, идемпотентность уведомлений и webhook, CHECK-ограничения, `demo_stage` только у демо-версий).
