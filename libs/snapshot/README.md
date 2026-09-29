# libs/snapshot — формат, writer, reader, SnapshotHolder, diff (C2, владелец R1)

**Статус:** этап 3 — ридер проверяет смещение и у пустых секций (находка `fuzz_snapshot_reader`).
Этап 2 — формат v1, writer, reader, holder, diff двух снапшотов.

## Назначение и границы
- Делает: бинарный формат снапшота ([docs/snapshot-format.md](../../docs/snapshot-format.md)), атомарную запись, чтение через `mmap` с полной проверкой, `SnapshotHolder` с атомарной заменой, diff двух снапшотов merge-join за O(N_old + N_new) (АРХ §7.3).
- НЕ делает: поиск и правила (`libs/verify`), работу с БД и запись `registry_change` (`apps/ingest`), выбор версии для загрузки (`apps/certd`).

## Ключевые файлы
| Файл | Что внутри |
|---|---|
| `include/sertkontrol/snapshot/format.hpp` | `FileHeader` (256 байт), `PackedRecord` (64 байта), секции, `serial_key`, `static_assert`-ы |
| `include/sertkontrol/snapshot/writer.hpp`, `src/writer.cpp` | `RecordInput`, `WriteOptions`, `write_snapshot` |
| `src/reader.cpp` | `open_snapshot` (C2) → `MappedSnapshot` |
| `include/sertkontrol/snapshot/holder.hpp` | `SnapshotHolder` на `std::atomic<std::shared_ptr>` |
| `include/sertkontrol/snapshot/diff.hpp`, `src/diff.cpp` | `DocState`, `DocChange`, `DiffStats`, `diff` |
| `include/sertkontrol/snapshot/lookup.hpp` | `find_index(snap, canonical)` — точный поиск записи (XXH3 → `equal_range` → сравнение строки) для потребителей вне `libs/verify` (`certd`: уведомления, сброс демо) |

## Публичный интерфейс
C2 (`sk::snapshot::Snapshot`, `open_snapshot`) — в контрактном заголовке; writer, holder и diff — здесь (diff — внутренний интерфейс R1, не контракт). Цель CMake — `sk::snapshot`.

## Зависимости
- Зависит от: `sk::contracts`, `sk::canon` (канонизация и ключи при записи), `libxxhash-dev`.
- От него зависят: `libs/verify`, `apps/ingest`, `apps/certd`.

## Потоки
Поток B (сборка) — writer, затем diff с предыдущей версией (шаг 2); поток A (проверка) — reader + holder (АРХ §4).

## Инварианты и «почему так»
- AoS для записей и отдельный плотный массив ключей — АРХ §6.
- `PackedRecord` читается `memcpy`, ключи — `span` в отображение: почему — docs/snapshot-format.md, «Время жизни».
- Дубли номера: остаётся запись с поздней `status_date`; число отброшенных — в `WriteStats::duplicates`.
- Ридер не доверяет файлу: 8 групп проверок при открытии; после открытия доступ без проверок.
- Holder: обработчик берёт `get()` один раз на запрос (АРХ §7.4).
- Diff опирается на полный порядок writer-а `(key_hash, каноническая строка)` без повторов номера: при равных ключах сравниваются строки, поэтому коллизии XXH3 не склеивают разные документы. Сравниваемое состояние — `(status, expiry_date, status_date)` (АРХ §7.3); записи с различающимися хэшами не распаковываются.

## Тесты
`tests/snapshot/` → `snapshot_test`: roundtrip, детерминированность, дубли, обрезка UTF-8, `by_serial`, `by_registry_id` (в т. ч. первое обращение из 4 потоков), 13 видов повреждений (включая с верной контрольной суммой), holder под 4 потоками-читателями; `diff_test` — все виды изменений, пустые снапшоты, property-тест против наивного `std::map` на 150 случайных парах, `find_index`.

## Ограничения и отложенное
Writer держит данные в памяти (достаточно для демо; потоковая сборка — с адаптером набора ФСА). TSan-тест swap — этап 3.
