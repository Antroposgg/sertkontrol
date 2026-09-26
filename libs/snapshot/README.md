# libs/snapshot — формат, writer, reader, SnapshotHolder (C2, владелец R1)

**Статус:** этап 1 — формат v1, writer, reader, holder. Diff — этап 2.

## Назначение и границы
- Делает: бинарный формат снапшота ([docs/snapshot-format.md](../../docs/snapshot-format.md)), атомарную запись, чтение через `mmap` с полной проверкой, `SnapshotHolder` с атомарной заменой.
- НЕ делает: поиск и правила (`libs/verify`), работу с БД (`apps/ingest`), выбор версии для загрузки (`apps/certd`).

## Ключевые файлы
| Файл | Что внутри |
|---|---|
| `include/sertkontrol/snapshot/format.hpp` | `FileHeader` (256 байт), `PackedRecord` (64 байта), секции, `serial_key`, `static_assert`-ы |
| `include/sertkontrol/snapshot/writer.hpp`, `src/writer.cpp` | `RecordInput`, `WriteOptions`, `write_snapshot` |
| `src/reader.cpp` | `open_snapshot` (C2) → `MappedSnapshot` |
| `include/sertkontrol/snapshot/holder.hpp` | `SnapshotHolder` на `std::atomic<std::shared_ptr>` |

## Публичный интерфейс
C2 (`sk::snapshot::Snapshot`, `open_snapshot`) — в контрактном заголовке; writer и holder — здесь. Цель CMake — `sk::snapshot`.

## Зависимости
- Зависит от: `sk::contracts`, `sk::canon` (канонизация и ключи при записи), `libxxhash-dev`.
- От него зависят: `libs/verify`, `apps/ingest`, `apps/certd`.

## Потоки
Поток B (сборка) — writer; поток A (проверка) — reader + holder (АРХ §4).

## Инварианты и «почему так»
- AoS для записей и отдельный плотный массив ключей — АРХ §6.
- `PackedRecord` читается `memcpy`, ключи — `span` в отображение: почему — docs/snapshot-format.md, «Время жизни».
- Дубли номера: остаётся запись с поздней `status_date`; число отброшенных — в `WriteStats::duplicates`.
- Ридер не доверяет файлу: 8 групп проверок при открытии; после открытия доступ без проверок.
- Holder: обработчик берёт `get()` один раз на запрос (АРХ §7.4).

## Тесты
`tests/snapshot/` → `snapshot_test`: roundtrip, детерминированность, дубли, обрезка UTF-8, `by_serial`, 13 видов повреждений (включая с верной контрольной суммой), holder под 4 потоками-читателями.

## Ограничения и отложенное
Writer держит данные в памяти (достаточно для демо; потоковая сборка — с адаптером набора ФСА). Diff и property-тест против `std::map` — этапы 2–3; TSan-тест swap — этап 3.
