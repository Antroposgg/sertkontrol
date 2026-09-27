# libs/maxapi — Bot API MAX, initData, события, C9 (владелец R4)

**Статус:** этап 2 — initData, секрет webhook, разбор `Update`, C9 `OutgoingMessage`, HTTP-клиент, двухуровневый лимитер исходящих (АРХ §7.6).

## Назначение и границы
- Делает: транспорт MAX — проверку подлинности запросов, разбор событий, сборку сообщений по схеме MAX с проверкой лимитов, вызовы Bot API, лимитер частоты исходящих (token bucket глобально и на чат).
- НЕ делает: доменную логику, тексты карточек (это `apps/certd/bot`), доступ к БД (очередь outbox и цикл отправки — `apps/certd`).

## Ключевые файлы
| Файл | Что внутри |
|---|---|
| `include/sertkontrol/maxapi/auth.hpp`, `src/auth.cpp` | `validate_init_data` (официальный алгоритм, `auth_date ≤ 24 ч`), `secret_matches` (`CRYPTO_memcmp`), `percent_encode/decode` |
| `include/sertkontrol/maxapi/update.hpp`, `src/update.cpp` | `parse_update` → `MessageCreated` / `MessageCallback` / `BotStarted` / `OtherUpdate`, `dedup_key` |
| `include/sertkontrol/maxapi/message.hpp`, `src/message.cpp` | C9 `OutgoingMessage`, `Button`, `validate` (лимиты MAX), `to_outbox_json` / `from_outbox_json`, `to_new_message_body`, `html_escape` |
| `include/sertkontrol/maxapi/bot_api.hpp`, `src/http_bot_api.cpp` | Интерфейс `BotApi`, `HttpBotApi` (Drogon `HttpClient`), `split_url` |
| `include/sertkontrol/maxapi/rate_limit.hpp`, `src/rate_limit.cpp` | `TokenBucket`, `SendLimiter` (глобально C = 5, r = 25/с; на чат C = 1, r = 1/с), `Permit` |
| `include/sertkontrol/maxapi/fake_bot_api.hpp` | `RecordingBotApi` — для тестов бота и outbox |

## Факты MAX API, на которые опирается модуль
Сверены 26.09.2026 с dev.max.ru и `schema.yaml` официального Go-клиента — таблица в [docs/plan.md §5.1](../../docs/plan.md). Главное: `https://platform-api2.max.ru`, токен в `Authorization`, TLS на сертификате Минцифры ([ADR-0012](../../docs/adr/0012-mintsifry-root-ca.md)), `text ≤ 4000`, `callback.payload ≤ 1024`, `open_app` требует `web_app`.

## Зависимости
- Зависит от: `sk::contracts`, Drogon (HTTP-клиент, jsoncpp), OpenSSL.
- От него зависят: `apps/certd` (webhook, REST, бот, outbox).

## Инварианты и безопасность
- Токен бота уходит только на `base_url` в `Authorization`; при скачивании вложений не отправляется.
- Скачивание — только `https://` (кроме тестов), с лимитом размера; хосты вложений MAX не документированы, поэтому список разрешённых хостов не задан — пункт ручной проверки.
- initData и секрет сравниваются за константное время.
- Лимитер: в любом окне τ не больше C + r·τ вызовов (АРХ §7.6) — при τ = 1 с это 30 запросов всего и 2 сообщения в чат, ровно лимиты MAX при любом способе их подсчёта. Токены списываются из обоих вёдер вместе; нехватка токена чата не тратит глобальный и не задерживает другие чаты. Полные вёдра чатов удаляются каждые 1024 вызова — память не растёт с числом пользователей.

## Потоки
Поток A, шаги 1 и 5 (АРХ §4); поток B, шаг 5 — лимитер отправителя outbox.

## Тесты
`tests/maxapi/` → `maxapi_test` (лимитер: формула ведра, пауза после 429, чистка вёдер, property «≤ C + r·τ в любом окне» на 8 000 случайных запросах глобально и по чатам; initData: тест-вектор из независимой реализации, 8 видов подделки, неполные подписанные данные, срок; события по фикстурам `tests/fixtures/max/`; C9 roundtrip; `NewMessageBody`; лимиты) и `maxapi_http_test` (HTTP-клиент против локального сервера Drogon: путь, query, заголовок, тело, коды 401/400/429/5xx, скачивание, недоступный сервер).

## Ограничения
- Фикстуры событий собраны по схеме, а не записаны с настоящего webhook — ручная проверка в MAX.
- Нужна ли авторизация при скачивании по `payload.url` и сколько живёт ссылка — в документации не указано.
- Идемпотентности отправки в MAX API нет — исходящие at-least-once (АРХ §7.5).
