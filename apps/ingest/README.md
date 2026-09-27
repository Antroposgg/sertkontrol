# apps/ingest — сборка снапшотов реестра (владелец R1)

**Статус:** этап 2 — `--demo` собирает демо-пару N/N+1, считает diff, пишет `registry_change` и публикует версии с `NOTIFY snapshot_ready`; `--daemon` запускает `--once` ежедневно в 04:00 МСК. `--once` — заглушка: источник ФСА не подтверждён.

## Назначение и границы
- Делает: читает источник через C3-адаптер, нормализует записи (канонизация, статусы, даты), применяет quality gate, пишет снапшот (`libs/snapshot`), регистрирует версию в PostgreSQL (C8).
- Делает также: diff новой версии с предыдущей (`libs/snapshot`) и запись изменений в `registry_change` в одной транзакции с переводом версии в `ready` и `NOTIFY snapshot_ready, '<v>'` (C8).
- НЕ делает: обслуживание запросов и уведомления пользователей (`certd`); общение с `certd` по сети — только файл в общем томе, строки `snapshot_version`/`registry_change` и `NOTIFY` (ADR-0002).

## Ключевые файлы
| Файл | Что внутри |
|---|---|
| `source_adapter.hpp` | **C3**: concept `SourceAdapter`, `RawRecord`, `RecordSink` |
| `demo_source.hpp/.cpp` | `DemoTsvSource` — адаптер `data/demo/*.tsv` |
| `fake_source.hpp` | `FakeSource` — тестовый адаптер |
| `normalize.hpp/.cpp` | `status_from_source`, `parse_iso_date`, `normalize`, `normalize_all` + quality gate 5% |
| `build.hpp` | `build_snapshot(adapter, dir, version, is_demo)`, имя файла `snap-<v>.bin` |
| `registry_db.hpp/.cpp` | `pg_conninfo(env)`, `VersionInfo`, `RegistryDb`: `mark_building` → `publish` (транзакция: `registry_change` + `ready` + `NOTIFY`) / `mark_failed`; JSON `doc_state_json`, `stats_json` (libpq) |
| `demo_pair.hpp/.cpp` | `run_demo_pair`: N (`base.tsv`, v1, `demo_stage = 0`) и N+1 (`next.tsv`, v2, `demo_stage = 1`) + diff |
| `schedule.hpp` | `next_daily_run` — следующий запуск в 04:00 МСК |
| `cli.hpp/.cpp`, `main.cpp` | Режимы запуска |

## Режимы
| Режим | Этап | Что делает |
|---|---|---|
| `--demo [--demo-dir D] [--snapshot-dir S]` | 2 | `D/base.tsv` → `S/snap-1.bin` (`demo_stage = 0`), `D/next.tsv` → `S/snap-2.bin` (`demo_stage = 1`); diff N → N+1 → `registry_change` версии 2; обе версии `ready`, по `NOTIFY` на каждую |
| `--once` | после подтверждения источника | однократная сборка из набора ФСА; сейчас только сообщает, что источник не подтверждён |
| `--daemon` | 2 | `--once` ежедневно в 04:00 МСК; SIGTERM/SIGINT — выход в течение секунды |

В `compose.yaml` это два сервиса: одноразовый `ingest-demo` (`--demo`, после `migrate`; `certd` стартует после него) и постоянный `ingest` (`--daemon`).

Переменные БД — те же, что у `certd`: `POSTGRES_HOST/PORT/DB/USER/PASSWORD`.

## Куда добавлять источник
Класс, удовлетворяющий `SourceAdapter` (имя, `source_date`, потоковый `for_each`), + `static_assert(SourceAdapter<…>)` + тест на фикстуре. Настоящий набор ФСА — только после проверки свежести и формата (АРХ §1, главный риск).

## Зависимости
- Зависит от: `sk::contracts`, `sk::canon`, `sk::snapshot`, libpq.
- От него зависят: никто (процесс верхнего уровня). `certd` читает результат через `snapshot_version`.

## Инварианты
- Quality gate: отвергнуто > 5% записей — сборка прерывается, версия помечается `failed` (АРХ §4).
- Повторный `--demo` перезаписывает те же версии 1 и 2 тем же содержимым (сборка детерминирована), строки `snapshot_version` сбрасываются в `building` и снова становятся `ready`, `registry_change` версии заменяется целиком. Номера демо-версий постоянны; certd различает их по `demo_stage`.
- `registry_change`, `ready` и `NOTIFY` — одна транзакция: PostgreSQL доставляет уведомление только после COMMIT, поэтому certd не увидит версию без её изменений. Ошибка внутри — ROLLBACK, версия остаётся `building`.
- Для демо в `registry_change` пишутся все изменения N → N+1 (их пять); для боевого набора — только наблюдаемые ключи (АРХ §4) — вместе с адаптером ФСА. Уведомления от `registry_change` не зависят: certd сравнивает портфель со снапшотом, `registry_change` — история документа.
- Расписание — фиксированное смещение UTC+3 (Москва без перехода на летнее время с 2014 г.); запуск строго после текущего момента, поэтому в 04:00:00 он не повторяется.

## Поток
Поток B, шаги 1–2 (АРХ §4): сборка, diff, `registry_change`, `ready`, `NOTIFY`.

## Тесты
`tests/ingest/` → `ingest_test`: CLI, `FakeSource`, статусы, даты, нормализация, TSV (6 видов повреждений), сборка `base.tsv`, quality gate, `pg_conninfo`, JSON `registry_change`/`stats`, расписание (граница 04:00, переход суток и года), пять изменений N → N+1 без БД; на PostgreSQL (через `scripts/ci/with-pg.sh`) — жизненный цикл версии, откат транзакции, CHECK `demo_stage`, публикация пары дважды (идемпотентность) и четыре `NOTIFY snapshot_ready` через отдельное соединение с `LISTEN`.
