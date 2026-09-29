# fuzz — цели libFuzzer (АРХ §10)

**Статус:** этап 3 — 7 целей, smoke 60 с на цель в CI (параллельно).

| Цель | Что проверяет | Свойства сверх «не падает» |
|---|---|---|
| `fuzz_canon` | `canonicalize`, `parse`, `find_numbers` (C1) | канонизация идемпотентна; `parse` согласован; `find_numbers` возвращает только канонизируемое |
| `fuzz_snapshot_reader` | `open_snapshot` на битых файлах (C2); XXH3 пересчитывается, чтобы доходить до структурных проверок | открытый снапшот читается целиком |
| `fuzz_update_json` | `parse_update` — тело webhook MAX | — |
| `fuzz_init_data` | `validate_init_data`, `percent_decode` | — |
| `fuzz_outbox_json` | строка outbox (C9) → лимиты MAX → `NewMessageBody` | — |
| `fuzz_demo_tsv` | парсер источника (C3): `DemoTsvSource` + нормализация | — |
| `fuzz_weighted_distance` | взвешенный Левенштейн (АРХ §7.2) | симметрия, 0 на равных строках, ≤ длины длинной строки |

Стартовые корпуса — `corpus/<цель>/` (в том числе `regression-*` — входы найденных ошибок). CSV-импорт (F9) — этап 4.

## Запуск

```bash
scripts/ci/fuzz-smoke.sh
```

`SK_FUZZ_SECONDS` — время на цель (по умолчанию 60). Сборка — пресет `fuzz` (clang 18, `-fsanitize=fuzzer,address,undefined`).
Находка: ненулевой код, вход — `build/fuzz/artifacts/<цель>-crash-*`, лог — `build/fuzz/logs/<цель>.log`.
Воспроизвести: `build/fuzz/fuzz/<цель> build/fuzz/artifacts/<файл>`.

## Найдено на этапе 3

| Цель | Ошибка | Исправление |
|---|---|---|
| `fuzz_outbox_json` | `Json::Value::asBool()` на строке — исключение jsoncpp уронило бы отправитель outbox | типы полей проверяются до чтения |
| `fuzz_update_json` | `operator[]` на не-объекте (`"user": 0`) — исключение на входящем webhook | безопасный `field()` |
| `fuzz_update_json` | `isIntegral()` пропускает числа > int64, `asInt64()` бросает | `isInt64()` / `isUInt64()` (также initData и outbox) |
