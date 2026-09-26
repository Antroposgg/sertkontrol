# libs/maxapi — Bot API MAX, initData, события, C9 (владелец R4)

**Статус:** этап 1 — initData, секрет webhook, разбор `Update`, C9 `OutgoingMessage`, HTTP-клиент. Отправитель outbox с двухуровневым token bucket — этап 2.

## Назначение и границы
- Делает: транспорт MAX — проверку подлинности запросов, разбор событий, сборку сообщений по схеме MAX с проверкой лимитов, вызовы Bot API.
- НЕ делает: доменную логику, тексты карточек (это `apps/certd/bot`), доступ к БД.

## Ключевые файлы
| Файл | Что внутри |
|---|---|
| `include/sertkontrol/maxapi/auth.hpp`, `src/auth.cpp` | `validate_init_data` (официальный алгоритм, `auth_date ≤ 24 ч`), `secret_matches` (`CRYPTO_memcmp`), `percent_encode/decode` |
| `include/sertkontrol/maxapi/update.hpp`, `src/update.cpp` | `parse_update` → `MessageCreated` / `MessageCallback` / `BotStarted` / `OtherUpdate`, `dedup_key` |
| `include/sertkontrol/maxapi/message.hpp`, `src/message.cpp` | C9 `OutgoingMessage`, `Button`, `validate` (лимиты MAX), `to_outbox_json` / `from_outbox_json`, `to_new_message_body`, `html_escape` |
| `include/sertkontrol/maxapi/bot_api.hpp`, `src/http_bot_api.cpp` | Интерфейс `BotApi`, `HttpBotApi` (Drogon `HttpClient`), `split_url` |
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

## Потоки
Поток A, шаги 1 и 5 (АРХ §4); поток B, шаг 5 — этап 2.

## Тесты
`tests/maxapi/` → `maxapi_test` (initData: тест-вектор из независимой реализации, 8 видов подделки, неполные подписанные данные, срок; события по фикстурам `tests/fixtures/max/`; C9 roundtrip; `NewMessageBody`; лимиты) и `maxapi_http_test` (HTTP-клиент против локального сервера Drogon: путь, query, заголовок, тело, коды 401/400/429/5xx, скачивание, недоступный сервер).

## Ограничения
- Фикстуры событий собраны по схеме, а не записаны с настоящего webhook — ручная проверка в MAX.
- Нужна ли авторизация при скачивании по `payload.url` и сколько живёт ссылка — в документации не указано.
- Идемпотентности отправки в MAX API нет — исходящие at-least-once (АРХ §7.5).
