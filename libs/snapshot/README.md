# libs/snapshot — формат, writer, reader, diff (C2, владелец R1)

**Статус:** этап 1 (формат v1, writer, reader), этап 2 (diff). Сейчас потребители используют `sk::fake::FakeSnapshot`.

## Назначение и границы
- Делает: бинарный формат снапшота (АРХ §6), запись `snap-<v>.bin.tmp` → `fsync` → `rename`, чтение через `mmap` + `std::memcpy`, merge-join diff (АРХ §7.3).
- НЕ делает: поиск и правила (это `libs/verify`), работу с БД (это `apps/ingest`).

## Публичный интерфейс
`sk::snapshot::open_snapshot` → `Result<SnapshotPtr>`; реализация интерфейса `Snapshot` из C2. Описание формата — `docs/snapshot-format.md` (этап 1).

## Зависимости
- Зависит от: `sk::contracts`, `libs/canon`, `libxxhash-dev`.
- От него зависят: `libs/verify`, `apps/ingest`, `apps/certd`.

## Инварианты и «почему так»
- Little-endian, секции выровнены на 64 байта, `sizeof(PackedRecord) == 64`, запись читается `memcpy` (C++20 не разрешает неявное создание объектов в `mmap`) — АРХ §7.4, ADR-0001.
- Ключи отсортированы по `(hash, каноническая строка)`; тот же порядок использует diff.

## Тесты (план)
Roundtrip writer → reader, битые файлы (fuzz ридера), property «diff против `std::map`» (этап 3).
