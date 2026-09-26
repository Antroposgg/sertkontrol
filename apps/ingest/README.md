# apps/ingest — сборка снапшотов реестра (владелец R1)

**Статус:** этап 0 — разбор режимов и контракт C3; сами режимы — заглушки (печатают сообщение и выходят с кодом 0).

## Назначение и границы
- Делает: (этап 1) `--demo` собирает демо-пару N/N+1 из `data/demo`; (этап 2) diff, `registry_change`, `snapshot_version`, `NOTIFY snapshot_ready`; `--daemon` — ежедневно в 04:00 МСК; `--once` — ручной запуск.
- НЕ делает: обслуживание запросов (это `certd`); разговор с `certd` по сети — только файл в томе и `NOTIFY` (ADR-0002).

## Ключевые файлы
| Файл | Что внутри |
|---|---|
| `source_adapter.hpp` | **C3** concept `SourceAdapter`, `RawRecord`, `RecordSink` |
| `fake_source.hpp` | `FakeSource` — C3 из заданных записей (`static_assert(SourceAdapter<FakeSource>)`) |
| `cli.hpp/.cpp` | `parse_args` → `Options{mode, snapshot_dir, demo_dir}`, `usage()` |
| `main.cpp` | Точка входа |

## Куда добавлять источник
Новый класс, удовлетворяющий `SourceAdapter` (имя, `source_date`, потоковый `for_each`), + `static_assert` + тест на фикстуре. Реальный набор ФСА — только после подтверждения свежести и формата (АРХ §1, главный риск).

## Зависимости
- Зависит от: `sk::contracts`; (этап 1+) `libs/snapshot`, `libs/canon`, PostgreSQL.
- От него зависят: никто (процесс верхнего уровня).

## Тесты
`tests/ingest/` → `ingest_test`: режимы и ошибки CLI, `FakeSource`.

## Поток
Поток B (АРХ §4).
