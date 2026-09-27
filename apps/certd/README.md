# apps/certd — сервер: домен, REST, webhook, бот, outbox, мини-приложение (владелец R3; bot/ — R4)

**Статус:** этап 1 — `DomainServiceImpl` (PostgreSQL + снапшот), REST `/api/v1`, webhook MAX, бот, отправитель outbox,
загрузка снапшота, `/healthz`, раздача мини-приложения.

## Назначение и границы
- Делает: поток A целиком (АРХ §4): webhook → бот → `DomainService` → распознавание (пул) → вердикт → outbox → MAX;
  REST для мини-приложения; загрузка актуального снапшота по `snapshot_version`.
- НЕ делает: сборку снапшота (`apps/ingest`), правила вердикта (`libs/verify`), транспорт MAX (`libs/maxapi`).

## Ключевые файлы
| Файл | Что внутри |
|---|---|
| `domain.hpp` | **C6** `DomainService` и DTO (`UserContext{max_user_id, channel}`, `CheckResult`, `PortfolioItem`, `Page`, …) |
| `domain_service.hpp/.cpp` | `DomainServiceImpl`: пользователи, согласие, журнал `check_log` с пачками, портфель, ИНН; все запросы с фильтром по `user_id` |
| `fake_domain.hpp/.cpp` | `FakeDomainService` — C6 в памяти для тестов бота и REST |
| `rest_api.hpp/.cpp` | `/api/v1/*`: аутентификация (initData или dev-пользователь, ADR-0013), тонкие обработчики, RFC 9457 |
| `json_views.hpp/.cpp` | JSON по `openapi.yaml`, коды ошибок → HTTP |
| `webhook.hpp/.cpp` | `POST /max/webhook`: секрет → разбор → `inbound_update` → 200 → обработка в фоне |
| `bot/` | Диалоги бота — [README](bot/README.md) |
| `ports.hpp`, `pg_ports.*`, `memory_ports.hpp` | Порты `Outbox` (C9) и `InboundLog`; реализации на PostgreSQL и в памяти; `OutboxSender` |
| `snapshot_loader.*` | Загрузка последней `ready`-версии в `SnapshotHolder` (этап 1 — опрос раз в 5 с) |
| `recognition_pool.*` | Пул потоков распознавания с ограниченной очередью (ADR-0004) |
| `rate_limiter.*` | 30 проверок в минуту на пользователя (АРХ §10) |
| `media.*`, `clock.hpp` | Тип файла по сигнатуре; «сегодня» по Москве |
| `config.*`, `health.*`, `main.cpp` | Конфигурация из окружения, `/healthz`, сборка компонентов |

## Переменные окружения
`CERTD_PORT` (8080), `CERTD_THREADS` (0), `CERTD_DB_CONNECTIONS` (4), `POSTGRES_HOST/PORT/DB/USER/PASSWORD`,
`SNAPSHOT_DIR`, `WEB_ROOT`, `MAX_BOT_TOKEN` (пусто — бот выключен), `MAX_WEBHOOK_SECRET` (обязателен с токеном,
`[A-Za-z0-9_-]{5,256}`), `MAX_BOT_USERNAME` (для кнопок `open_app`), `MAX_API_BASE_URL`
(`https://platform-api2.max.ru`), `CERTD_DEV_USER_ID` (только без токена, ADR-0013), `CERTD_RECOG_THREADS` (2),
`CERTD_RECOG_QUEUE` (8).

## HTTP
| Путь | Ответ |
|---|---|
| `GET /healthz` | 200, если БД доступна и снапшот загружен; иначе 503 |
| `/api/v1/*` | по [`openapi.yaml`](../../openapi.yaml) (C7) |
| `POST /max/webhook` | 401 — неверный секрет; 400 — не событие; 200 — принято или повтор |
| `/` | статика `web/dist` |

## Куда добавлять эндпоинт
1. `openapi.yaml` (C7) + запись в docs/changelog-contracts.md.
2. Новая доменная операция — метод `DomainService` (C6) в `domain.hpp`, реализации в `domain_service.cpp` и `fake_domain.cpp`.
3. Тонкий обработчик в `rest_api.cpp` + маршрут в `register_routes`.
4. Тесты: `tests/certd/rest_api_test.cpp` (на фейке), `pg_test.cpp` (на БД); `openapi_test` проверит, что путь описан.

## Зависимости
- Зависит от: `sk::contracts`, `sk::canon`, `sk::snapshot`, `sk::verify`, `sk::recog`, `sk::maxapi`, Drogon 1.8.7, PostgreSQL 16.
- От него зависят: `web/` (REST).

## Инварианты и «почему так»
- Пользователь — только из проверенной initData или события webhook; каждый SQL-запрос фильтрует по его `user_id`;
  чужая запись неотличима от несуществующей (404) — АРХ §10.
- Только параметризованные запросы (`$1`); в выражениях с `$n` и литералами — явные приведения (`$6::bigint`),
  иначе libpq передаёт неверный двоичный формат.
- Снапшот берётся из `SnapshotHolder` один раз на запрос (АРХ §7.4).
- Webhook отвечает до обработки: MAX ждёт 200 не дольше 30 с и повторяет до 10 раз; повтор отсекается по `inbound_update`.
- Параметры корутин — по значению; никакого `?:` рядом с `co_await` (CLAUDE.md).
- Файлы пользователей не пишутся на диск (временный каталог Drogon — в `/tmp`).
- Если QR выписки содержит ссылку на реестр, она заменяет ссылку, построенную по `registry_id`.

## Тесты
`tests/certd/`: `certd_test` (конфиг, health, фейк домена, REST, бот, webhook, компоненты), `certd_pg_test`
(домен, IDOR, outbox, загрузчик снапшота — на PostgreSQL через `scripts/ci/with-pg.sh`), `openapi_test`
(пути `openapi.yaml` ≡ маршруты). `main.cpp` проверяется `scripts/ci/compose-smoke.sh`.

## Ограничения и отложенное
- `LISTEN snapshot_ready` и горячая замена снапшота — этап 2 (сейчас опрос `snapshot_version` раз в 5 с).
- Отправитель outbox — последовательный, 3 попытки с паузой 5·n с; token bucket и джиттер (АРХ §7.6) — этап 2.
- `simulate_update` / `reset_demo` отвечают 403 — этап 2.
- Разбор PDF — в процессе certd, без подпроцесса с `RLIMIT_*` (Should, АРХ §10).
