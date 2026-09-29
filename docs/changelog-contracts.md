# Журнал изменений контрактов C1–C9

Правило (АРХ §5): изменение контракта — PR с аппрувом владельцев поставщика и потребителя,
обновлённые golden-файлы и запись здесь. Записи — сверху вниз от новых к старым.

| Дата | Контракт | Изменение | Причина | Этап |
|---|---|---|---|---|
| 2026-09-29 | C6 | `DomainService::confirm(user, check_id)` → `CheckResult` | «Это номер …?» → «Да» (АРХ §4, поток A, шаг 6); кнопки `y:`/`n:` уже в C9 | 3 |
| 2026-09-29 | C4 | `Level::kNeedsConfirmation` начал выдаваться; правила `fuzzy.match`, `advice.confirm_number`; `Suggestion::distance` — взвешенное расстояние | Нечёткий поиск (АРХ §7.2) | 3 |
| 2026-09-28 | C4 | Правило `not_found.recent` (расчёт): номер не найден, а год в номере равен году даты данных → «мог быть зарегистрирован после <даты>, проверьте по QR» | Правило V5 каталога R2 не было реализовано на этапе 1 | 2 |
| 2026-09-27 | C9 | Действие callback `s:<check_id>` — «Указать поставщика» (ответ пользователя — ИНН, состояние диалога в `dialog_state`); вид сообщения `status_changed` рендерится ботом из `ChangeNotice` (`apps/certd/notify.hpp`) | F5 — уведомление о смене статуса; АРХ §8 «Интерфейс бота», кнопка `[Указать поставщика]` | 2 |
| 2026-09-27 | C7 | `openapi.yaml` 1.1.0: `GET /history?number=` → `DocumentHistory`, `POST /demo/simulate-update` → `200 DemoUpdate`, `POST /demo/reset` → `204`; `DataStatus.demo_stage`, `DataStatus.demo_update_available`; ответ `403 forbidden` | Экран «Документ» и демо-кнопки (F5, F6); отличия от АРХ §8 — [ADR-0014](adr/0014-history-and-demo-endpoints.md) | 2 |
| 2026-09-27 | C6 | Новые `attach_supplier(ctx, check_id, supplier_inn)` и `history(ctx, number)` → `DocumentHistory`; `simulate_update` → `Result<DemoUpdate>` (число уведомлений) вместо `Result<Ok>`; `DataStatus` + `demo_stage`, `demo_update_available`; реализации: `DomainServiceImpl`, `FakeDomainService` | Кнопка «Указать поставщика» (АРХ §8), история на экране «Документ», результат демо-кнопки (ADR-0014) | 2 |
| 2026-09-27 | C8 | Миграция `0003`: `snapshot_version.demo_stage` (0 — демо N, 1 — демо N+1, у боевых NULL; CHECK «только у демо»), `job.last_error`; `registry_change` пишется в одной транзакции с переводом версии в `ready` и `NOTIFY snapshot_ready, '<version>'` | F6: certd держит демо-пару открытой и различает её не по номеру версии; F5: версия не видна без своих изменений | 2 |
| 2026-09-26 | C9 | Действия callback: добавлены `W` (все из пачки), `c` (согласие), `h` (справка); `s`, `y`, `n` зарезервированы | Сводка «Поставить все на контроль», согласие при старте (АРХ §8, §10) | 1 |
| 2026-09-26 | C8 | Миграция `0002`: `check_log.batch_id` | Аргумент кнопки «Поставить все» — id пачки проверок, а не список номеров (АРХ §8) | 1 |
| 2026-09-26 | C7 | Первая редакция `openapi.yaml` (OpenAPI 3.1): `/me`, `/check`, `/check/file`, `/portfolio` (GET/POST), `/portfolio/{id}` (DELETE), `/data-status`, `/healthz`, `/max/webhook` | Этап 1 | 1 |
| 2026-09-26 | C6 | `UserContext.channel`; `check_text/check_file` → `CheckResult {batch_id, CheckedVerdict[]}`; новые `give_consent`, `add_checked`, `add_batch`; `Me.consented` | Кнопкам бота нужен id проверки (АРХ §8), согласие на обработку данных (АРХ §10), `check_log.via` | 1 |
| 2026-09-26 | C3 | В `RawRecord` добавлено поле `suspended_until` | Статус «приостановлен до …» есть в записи снапшота (C2) и в правиле `status.suspended` | 1 |
| 2026-09-26 | C1–C9 | Первая редакция: `libs/contracts/include/sertkontrol_contracts.hpp` (C1, C2, C4, C5), `apps/ingest/source_adapter.hpp` (C3), `apps/certd/domain.hpp` (C6), `db/migrations/0001_init.sql` (C8, C9), `docs/contracts/outgoing_message.schema.json` (C9). C7 (`openapi.yaml`) — этап 1 | Каркас | 0 |
| 2026-09-26 | C1 | Добавлена `canon::find_numbers(text, max_count)` сверх перечня АРХ (`canonicalize`, `parse`, `key_hash`) | F1: до 20 номеров в одном сообщении — выделение кандидатов из свободного текста принадлежит грамматике номера | 0 |
| 2026-09-26 | C2 | `Snapshot` — абстрактный интерфейс с `keys()`, `record(i)`, `by_serial()`; `open_snapshot` возвращает `Result<SnapshotPtr>` | Подстановка `FakeSnapshot` в тестах потребителей; ошибки открытия (версия canon, контрольная сумма) без исключений | 0 |
| 2026-09-26 | C6 | Добавлен `reset_demo` к перечню АРХ §8 | F6: «Сбросить демо» (постановка этапа 2) | 0 |
| 2026-09-26 | общие | `sk::Result<T>` и `sk::Error` вместо `std::expected` (C++23) | Стандарт проекта — C++20 (АРХ §9) | 0 |
