# libs/contracts — контракты C1, C2, C4, C5 и их fake

**Статус:** этап 0 — готово. Владелец правок: владельцы соответствующих контрактов (АРХ §5).

## Назначение и границы
- Делает: объявляет типы и сигнатуры контрактов между ролями и даёт fake-реализации для потребителей.
- НЕ делает: настоящую канонизацию, чтение снапшота, вердикт, распознавание — это `libs/canon`, `libs/snapshot`, `libs/verify`, `libs/recog` (этап 1).
- Почему отдельный модуль — [ADR-0008](../../docs/adr/0008-contracts-module.md).

## Ключевые файлы
| Файл | Что внутри |
|---|---|
| `include/sertkontrol_contracts.hpp` | `sk::Result`, `sk::Error`, `sk::ErrorCode`; C1 `sk::canon`; C2 `sk::snapshot`; C4 `sk::verify`; C5 `sk::recog` |
| `include/sertkontrol/fakes.hpp`, `src/fakes.cpp` | `sk::fake::*`: упрощённые канонизация, `FakeSnapshot` (3 записи), `check`, `recognize`, `format_date` |

## Публичный интерфейс
| Контракт | Сигнатуры | Реализация | Этап |
|---|---|---|---|
| C1 | `canon::canonicalize`, `parse`, `key_hash`, `find_numbers`, `kVersion` | `libs/canon` | 1 |
| C2 | `snapshot::Snapshot` (интерфейс), `RecordView`, `SnapshotMeta`, `open_snapshot` | `libs/snapshot` | 1 |
| C4 | `verify::check`, `Verdict`, `Finding` (`Basis`: факт / расчёт / рекомендация), `Card`, `Suggestion` | `libs/verify` | 1 |
| C5 | `recog::recognize`, `Found`, `Limits`, `MediaType` | `libs/recog` | 1 |

Цели CMake: `sk::contracts` (INTERFACE, без внешних зависимостей), `sk::fakes` (STATIC).

## Зависимости
- Зависит от: только стандартная библиотека C++20.
- От него зависят: все модули C++ (`apps/certd`, `apps/ingest`, будущие `libs/*`).

## Инварианты и «почему так»
- `RecordView` содержит `string_view` в отображение файла — валиден, пока жив `SnapshotPtr` (АРХ §7.4).
- `Verdict` и `Card` хранят только `std::string` — переживают `co_await` и замену снапшота (АРХ §7.4).
- Ключи снапшота отсортированы; поиск обязан пройти весь `equal_range` и сравнить строку — коллизии XXH3 ненулевые (АРХ §7.2). `fake::check` делает так же; тест `FakeCheck.HashCollisionComparesFullString`.
- `Result` — вместо `std::expected` (C++23). Хранение — `optional<T>` + `Error`, не `variant`: анализатор clang-tidy не моделирует `variant` и давал ложные срабатывания.
- `fake::key_hash` — FNV-1a, НЕ совместим с настоящим XXH3: fake-снапшот нельзя смешивать с настоящим `canon`.
- Все поля агрегатов имеют `{}` — [ADR-0010](../../docs/adr/0010-explicit-member-initializers.md).
- Коды `ErrorCode` совпадают с полем `code` RFC 9457 и с `ProblemCode` в `web/src/api/problem.ts` — меняются вместе.

## Потоки данных
Поток A (проверка): `canon` → `snapshot` → `verify` → `Verdict`; Поток B (обновление): `canon` → `snapshot` (АРХ §4).

## Тесты
`tests/contracts/` → бинарь `contracts_test`: `Result`, имена кодов и статусов, все fake.
```bash
scripts/dev.sh scripts/ci/cpp-build-test.sh gcc-release
```

## Ограничения и отложенное
Настоящие реализации C1, C2, C4, C5 готовы (этап 1): `libs/canon`, `libs/snapshot`, `libs/verify`, `libs/recog`.
Fake остаются тестовыми дублями — на них работает `FakeDomainService` (тесты бота и REST). Fake-канонизация не заменяет
кириллические гомоглифы, fake-вердикт знает одно правило статуса и срока — продакшн-код их не использует.
