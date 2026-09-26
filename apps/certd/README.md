# apps/certd — сервер: домен, REST, задачи, раздача мини-приложения (владелец R3)

**Статус:** этап 0 — конфиг, `/healthz`, статика `web/dist`, контракт C6 и `FakeDomainService`.

## Назначение и границы
- Делает: HTTP-сервер на Drogon; `GET /healthz`; раздача статики мини-приложения; (этап 1+) REST `/api/v1`, webhook MAX, `DomainService`, задачи и outbox.
- НЕ делает: сборку снапшота (это `apps/ingest`), транспорт MAX (это `libs/maxapi`), диалоги бота (это `apps/certd/bot`, R4).

## Ключевые файлы
| Файл | Что внутри |
|---|---|
| `domain.hpp` | **C6** `DomainService` (корутины `drogon::Task<Result<…>>`) и DTO: `UserContext`, `PortfolioItem`, `Page`, `DataStatus`, … |
| `fake_domain.hpp/.cpp` | `FakeDomainService` — C6 в памяти на `FakeSnapshot` (демо-стадии N/N+1 на пользователя, IDOR-изоляция) |
| `config.hpp/.cpp` | `load_config(EnvLookup)` — переменные окружения, валидация, строка подключения libpq |
| `health.hpp/.cpp` | `evaluate_health` — логика `/healthz` без HTTP |
| `main.cpp` | Сборка приложения: PG-клиент, `/healthz`, статика, запуск |

Цели CMake: `sk::certd_core` (всё, кроме `main`), исполняемый `certd`.

## Переменные окружения
`CERTD_PORT` (8080), `CERTD_THREADS` (0 = по ядрам), `CERTD_DB_CONNECTIONS` (4), `POSTGRES_HOST` (postgres), `POSTGRES_PORT` (5432), `POSTGRES_DB`, `POSTGRES_USER` (sertkontrol), `POSTGRES_PASSWORD` (пусто — без пароля, ADR-0009), `SNAPSHOT_DIR` (/data/snapshots), `WEB_ROOT` (/srv/app). С этапа 1: `MAX_BOT_TOKEN`, `MAX_WEBHOOK_SECRET`.

## HTTP
| Путь | Этап | Ответ |
|---|---|---|
| `GET /healthz` | 0 | 200 `{"status":"ok","db":"ok","snapshot_version":null}` / 503. С этапа 1 требует загруженный снапшот |
| `GET /` и статика | 0 | `web/dist` |
| `/api/v1/*` | 1–2 | по `openapi.yaml` (C7) |
| `POST /max/webhook` | 1 | 200 ≤ 100 мс, дедупликация по `inbound_update` |

## Куда добавлять эндпоинт
1. Описать в `openapi.yaml` (C7) и в таблице выше.
2. Добавить метод в `DomainService` (C6), если нужна новая доменная операция — запись в docs/changelog-contracts.md.
3. Обработчик — тонкий: разбор запроса → `DomainService` → JSON / RFC 9457. Фильтр по `user_id` из initData — всегда.
4. Тест: unit на `FakeDomainService` + контрактный тест по `openapi.yaml`.

## Зависимости
- Зависит от: `sk::contracts`, `sk::fakes` (до этапа 1), Drogon 1.8.7 (ADR-0003), PostgreSQL 16.
- От него зависят: `web/` (REST), `apps/certd/bot` (C6).

## Инварианты и «почему так»
- Параметры корутин — по значению; корутина-обработчик — метод класса, а не лямбда с захватом (захваты висят после приостановки).
- Снапшот берётся один раз на запрос (`FakeDomainService::snapshot_for`, АРХ §7.4).
- Чужая запись портфеля неотличима от несуществующей → `kNotFound` (IDOR, АРХ §10).
- Файлы пользователей на диск не пишутся; временный каталог Drogon — в `/tmp/certd-uploads`.

## Тесты
`tests/certd/` → `certd_test`: конфиг, health, `FakeDomainService` (портфель, фильтры, курсор, IDOR, демо-стадии). `main.cpp` проверяется compose-smoke (`scripts/ci/compose-smoke.sh`).

## Ограничения и отложенное
Нет REST, webhook, initData, снапшота — этап 1. Поток A — АРХ §4.
