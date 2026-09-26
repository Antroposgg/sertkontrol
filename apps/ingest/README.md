# apps/ingest — сборка снапшотов реестра (владелец R1)

**Статус:** этап 1 — `--demo` (снапшот N из `data/demo/base.tsv` + регистрация в `snapshot_version`). `--once` и `--daemon` — заглушки: источник ФСА не подтверждён, планировщик и diff — этап 2.

## Назначение и границы
- Делает: читает источник через C3-адаптер, нормализует записи (канонизация, статусы, даты), применяет quality gate, пишет снапшот (`libs/snapshot`), регистрирует версию в PostgreSQL (C8).
- НЕ делает: обслуживание запросов (`certd`); общение с `certd` по сети — только файл в общем томе и строка `snapshot_version` (`NOTIFY` — этап 2; ADR-0002).

## Ключевые файлы
| Файл | Что внутри |
|---|---|
| `source_adapter.hpp` | **C3**: concept `SourceAdapter`, `RawRecord`, `RecordSink` |
| `demo_source.hpp/.cpp` | `DemoTsvSource` — адаптер `data/demo/*.tsv` |
| `fake_source.hpp` | `FakeSource` — тестовый адаптер |
| `normalize.hpp/.cpp` | `status_from_source`, `parse_iso_date`, `normalize`, `normalize_all` + quality gate 5% |
| `build.hpp` | `build_snapshot(adapter, dir, version, is_demo)`, имя файла `snap-<v>.bin` |
| `registry_db.hpp/.cpp` | `pg_conninfo(env)`, `RegistryDb`: `building` → `ready` / `failed` (libpq) |
| `cli.hpp/.cpp`, `main.cpp` | Режимы запуска |

## Режимы
| Режим | Этап | Что делает |
|---|---|---|
| `--demo [--demo-dir D] [--snapshot-dir S]` | 1 | `D/base.tsv` → `S/snap-1.bin`, `snapshot_version(1, status='ready', is_demo)` |
| `--once` | после подтверждения источника | однократная сборка из набора ФСА |
| `--daemon` | 2 | ежедневно в 04:00 МСК |

Переменные БД — те же, что у `certd`: `POSTGRES_HOST/PORT/DB/USER/PASSWORD`.

## Куда добавлять источник
Класс, удовлетворяющий `SourceAdapter` (имя, `source_date`, потоковый `for_each`), + `static_assert(SourceAdapter<…>)` + тест на фикстуре. Настоящий набор ФСА — только после проверки свежести и формата (АРХ §1, главный риск).

## Зависимости
- Зависит от: `sk::contracts`, `sk::canon`, `sk::snapshot`, libpq.
- От него зависят: никто (процесс верхнего уровня). `certd` читает результат через `snapshot_version`.

## Инварианты
- Quality gate: отвергнуто > 5% записей — сборка прерывается, версия помечается `failed` (АРХ §4).
- Повторный `--demo` перезаписывает ту же версию тем же содержимым (сборка детерминирована), строка `snapshot_version` сбрасывается в `building` и снова становится `ready`.

## Поток
Поток B, шаги 1–2 (АРХ §4); на этапе 1 без diff и `NOTIFY`.

## Тесты
`tests/ingest/` → `ingest_test`: CLI, `FakeSource`, статусы, даты, нормализация, TSV (6 видов повреждений), сборка `base.tsv`, quality gate, `pg_conninfo`, жизненный цикл версии в PostgreSQL (через `scripts/ci/with-pg.sh`).
