# План реализации Сертконтроля

Источники истины:

- [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) (далее — АРХ) — стек, структура, контракты C1–C9, алгоритмы, CI;
- [`docs/CASE_REQUIREMENTS.md`](CASE_REQUIREMENTS.md) (далее — КЕЙС) — ограничения и формат сдачи. При конфликте КЕЙС важнее.

Работа идёт этапами 0–4. Этап начинается только по команде, заканчивается ворота-проверкой (см. [`CLAUDE.md`](../CLAUDE.md#ворота-проверка)).

**Текущий этап: 0 — каркас, завершён 2026-09-26 (ворота зелёные). Следующий — 1, по команде.**

---

## 1. Противоречия и пробелы АРХ ↔ КЕЙС

| # | Где | Суть | Решение |
|---|---|---|---|
| 1 | КЕЙС §4 п.3, п.5 «одна команда запуска» ↔ АРХ §3 `ingest --daemon` в 04:00 | Жюри поднимает стек `docker compose up`; если данные собираются только ночью, после старта снапшота нет и сценарий не проходится | Сервис `ingest` в `compose.yaml` при старте выполняет `--demo` (собирает пару N/N+1), затем работает как демон. Реализуется на этапах 1–2. На этапе 0 `--demo` — заглушка |
| 2 | КЕЙС §4 п.5 `.env.example` без секретов + «одна команда» ↔ АРХ §10: PostgreSQL требует пароль | Если пароль обязателен, `docker compose up` без `.env` падает; если зашить пароль в репозиторий — нарушение КЕЙС §2 п.8 | Локально PostgreSQL доступен только во внутренней сети compose (порт наружу не публикуется), метод аутентификации по умолчанию `trust`. На VPS пароль и `scram-sha-256` задаются в `.env`. См. [ADR-0009](adr/0009-local-run-without-secrets.md) |
| 3 | КЕЙС §4 «Только для решений с собственным API» ↔ АРХ §11 открытый вопрос | Неизвестно, считается ли `/api/v1` собственным API | `openapi.yaml` нужен в любом случае (C7). `DATA-API.yaml` и тестовые учётки готовим на этапе 3 как дешёвую страховку; вопрос остаётся открытым для организаторов |
| 4 | КЕЙС §2 п.3 «функциональность в обеих версиях MAX» ↔ F10 `openCodeReader` не поддержан в вебе | Бонусная функция недоступна в веб-клиенте | Не противоречие: основной сценарий не зависит от камеры, в вебе работает загрузка файла (F2). Кнопка скрыта по `WebApp.platform` |
| 5 | АРХ §3, §10 — образ `node:20` | Node.js 20 закончил поддержку 30.04.2026 (сегодня 26.09.2026) | `node:24` (Active LTS). См. [ADR-0007](adr/0007-node-24-lts.md) |
| 6 | АРХ §1, §5 — расположение `sertkontrol_contracts.hpp` не указано в структуре §3 | Нужен header-only модуль, от которого зависят все библиотеки | `libs/contracts/`. См. [ADR-0008](adr/0008-contracts-module.md) |
| 7 | АРХ §6 — кто применяет миграции | Не указано | Одноразовый сервис `migrate` в compose (образ `postgres:16`, `db/migrate.sh`, таблица `schema_migrations`); `certd` стартует после его успешного завершения. Не отклонение, а уточнение |
| 8 | КЕЙС §2 п.10, §7 п.4 — тестовые данные явно помечены | Данные ФСА не подтверждены | Весь MVP работает на `data/demo`. Пометка «тестовые данные» — в карточке, на экране «Данные», в README (этапы 1–3) |
| 9 | КЕЙС §4 п.5 «сборка ≤ 5 мин» ↔ АРХ «≤ 4 мин» | Нет противоречия, АРХ строже | Таймер в CI падает при > 240 с |
| 10 | АРХ C1 (`canonicalize`, `parse`, `key_hash`) ↔ F1 «до 20 номеров в сообщении» | В C1 нет функции выделения номеров из свободного текста | C1 дополнен `find_numbers(text, max_count)`. Запись в [`changelog-contracts.md`](changelog-contracts.md) |

Скиллы `architecture`, `coding-skills`, `documentation` в окружении не найдены: ни среди скиллов claude.ai, ни в `~/.claude`. Их требования взяты из постановки задачи (раздел «Документация» и «Общие правила»), они записаны в `CLAUDE.md`.

---

## 2. F1–F6 → модули → приёмка → тест → этап

| F | Требование (кратко) | Модули | Критерий приёмки (АРХ §2) | Тест | Этап |
|---|---|---|---|---|---|
| F1 | Номер текстом, любая раскладка, до 20 номеров | `libs/canon`, `libs/verify`, `apps/certd` (DomainService), `apps/certd/bot` | Номер с 1–2 ошибками находится; ответ ≤ 1 с | `tests/canon/golden_test` (3 строки из §7.1), `tests/verify/exact_test` — этап 1; `tests/verify/fuzzy_test`, property «нечёткий vs перебор», `bench/search_bench` (p99 ≤ 1 / ≤ 5 мс) — этап 3 | 1 (точный), 3 (нечёткий) |
| F2 | PDF: QR → текстовый слой | `libs/recog`, `apps/certd` | Выписка 89369/26 → верный номер и ссылка на реестр | `tests/recog/pdf_test` на фикстуре выписки (без персональных данных), лимиты размера/страниц | 1 |
| F3 | Карточка вердикта с метками «факт / расчёт / рекомендация», дата данных; «нет в данных на <дату>» + ближайшие номера | `libs/verify`, `apps/certd/bot`, `web/` | Все поля и метки на месте; ненайденный номер → «нет в данных на <дату>» и ближайшие | `tests/verify/rules_test` (табличный, по `docs/rules.md`), `tests/certd/card_render_test`, `web` `DocumentCard.test.tsx` | 1 |
| F4 | Портфель: на контроль, список с фильтрами, удалить — из бота и мини-приложения | `apps/certd` (DomainService, REST), `db/`, `apps/certd/bot`, `web/` | Доступен из бота и из мини-приложения | `tests/certd/portfolio_test` (PG в compose), IDOR «чужой id → 404», контрактные тесты по `openapi.yaml` (этап 3), `web` `Portfolio.test.tsx` | 1 |
| F5 | Ежедневное обновление → diff → уведомление | `apps/ingest`, `libs/snapshot` (diff), `apps/certd` (LISTEN, `notify_changes`, outbox), `libs/maxapi` (отправитель) | Смена статуса наблюдаемого документа → ровно одно сообщение ≤ 15 мин после готовности снапшота | `tests/snapshot/diff_test`, property «diff vs std::map» (этап 3), `tests/certd/notify_changes_test` (идемпотентность), `tests/maxapi/token_bucket_test`, интеграционный тест в compose | 2 |
| F6 | Демо-режим N / N+1, «Симулировать обновление», всё помечено тестовым | `data/demo`, `apps/ingest --demo`, `apps/certd` (`demo_stage`, `/demo/simulate-update`), `web/` («Данные») | Проверяющий проходит сценарий одним аккаунтом за 5 минут | `tests/integration/jury_scenario` в compose (webhook-фикстуры → outbox), чек-лист E2E в README | 2 |

F7–F10 (Should) — этап 4 по отдельной команде. F11–F12 (Could) — вне плана.

---

## 3. Этап 0 — каркас (детальный план)

Цель: пустой, но полностью «зелёный» по воротам репозиторий, где у каждого модуля есть место, контракт, fake, README и CI.

| # | Шаг | Результат | Ворота |
|---|---|---|---|
| 0.1 | `git init`, `.gitignore`, `.editorconfig`, LICENSE (GPL-3.0-or-later) | Репозиторий | 9 |
| 0.2 | `docs/plan.md` (этот файл), ADR 0001–0006 из АРХ §9 + 0007–0011 (противоречия и решения этапа) | `docs/adr/` | 7, 8 |
| 0.3 | CMake 3.28 + Ninja + `CMakePresets.json`: `gcc-release`, `clang-asan`, `coverage`, `tidy`. Флаги `-Wall -Wextra -Wpedantic -Werror` (+ `-Wconversion -Wshadow` и др.) для GCC 13 и clang 18 | `CMakeLists.txt`, `cmake/*.cmake` | 1 |
| 0.4 | `.clang-format`, `.clang-tidy` (WarningsAsErrors: `*`) | Конфиги | 1 |
| 0.5 | Контракты C1, C2, C4, C5 — `libs/contracts/include/sertkontrol_contracts.hpp`; C3 — `apps/ingest/source_adapter.hpp` (concept); C6 — `apps/certd/domain.hpp`; C8 — SQL-миграция; C9 — `docs/contracts/outgoing_message.schema.json` + таблица `outbox` | Контракты с doxygen | 1, 7 |
| 0.6 | Fake-реализации: `FakeSnapshot` (3 записи), `fake::canonicalize/parse/key_hash`, `fake::check`, `fake::recognize`, `FakeSource`, `FakeDomainService` | `libs/contracts/fakes`, `apps/certd/fake_domain.*`, `apps/ingest/fake_source.hpp` | 2, 3 |
| 0.7 | Миграция `db/migrations/0001_init.sql` — все 11 таблиц АРХ §6; `db/migrate.sh` идемпотентный | `db/` | 4 (тест: применить дважды) |
| 0.8 | `certd`: конфиг из окружения, `GET /healthz` (БД доступна; снапшот — с этапа 1), раздача статики `web/dist` | `apps/certd` | 5 |
| 0.9 | `ingest`: разбор режимов `--once / --daemon / --demo`; сами режимы — заглушки | `apps/ingest` | 2 |
| 0.10 | Unit-тесты GoogleTest на всё выше; `ctest --repeat until-fail:2` | `tests/` | 2, 3 |
| 0.11 | `web/`: Vite + React + TS strict, ESLint (flat config, typescript-eslint strict), Vitest + coverage-v8 (порог 70%), API-клиент с `X-Max-Init-Data` и RFC 9457, оболочка приложения с пометкой «тестовые данные» | `web/` | 1–3 |
| 0.12 | `Dockerfile` по скелету АРХ §10 (+ `node:24`, непривилегированный пользователь), `compose.yaml` (`postgres`, `migrate`, `certd`, `ingest`, `caddy` в профиле `prod`), `.env.example`, `.dockerignore` | Docker | 5 |
| 0.13 | Скрипты `scripts/ci/*.sh` — единые для GitHub Actions, dev-контейнера и хоста; `docker/dev.Dockerfile` для локального прогона | Скрипты | 4 |
| 0.14 | `.github/workflows/ci.yml`: запреты импорта (`deps-check.sh`), GCC Release, clang ASan/UBSan, clang-tidy, clang-format, покрытие (gcovr ≥ 70%), миграции на PG 16, web lint/tsc/test/build, `docker build --no-cache` ≤ 240 с + compose smoke `/healthz`, gitleaks | CI | 4 |
| 0.15 | `CLAUDE.md`, `AGENTS.md`, README в каждом модуле, корневой README-заготовка | Документация | 8 |
| 0.16 | Ворота-проверка 1–9, коммиты, отчёт | — | все |

### Заглушки этапа 0 (fake) и когда они заменяются

| Заглушка | Где | Заменяется на | Этап |
|---|---|---|---|
| `fake::canonicalize/parse/key_hash/find_numbers` (упрощённые правила, FNV-1a вместо XXH3) | `libs/contracts/fakes` | `libs/canon` | 1 |
| `FakeSnapshot` (3 записи в памяти) | `libs/contracts/fakes` | `libs/snapshot` (формат v1, mmap) | 1 (остаётся как тестовый дубль) |
| `fake::check` (только точное совпадение, одно правило статуса) | `libs/contracts/fakes` | `libs/verify` | 1 |
| `fake::recognize` (заранее заданный ответ) | `libs/contracts/fakes` | `libs/recog` | 1 |
| `FakeDomainService` (в памяти) | `apps/certd/fake_domain.*` | `DomainServiceImpl` (PG + снапшот) | 1 (остаётся для тестов бота) |
| `FakeSource` | `apps/ingest/fake_source.hpp` | Демо-адаптер `data/demo`, затем адаптер набора ФСА | 1 / после подтверждения данных |
| `ingest --demo / --once / --daemon` пишут «не реализовано» и выходят с кодом 0 | `apps/ingest/main.cpp` | Реальные режимы | 1–2 |
| `/healthz` не требует снапшот | `apps/certd/health.cpp` (`snapshot_required=false`) | Требовать загруженный снапшот | 1 |

---

### Результат ворот этапа 0 (чистый клон, `scripts/gate.sh`)

| # | Пункт | Результат |
|---|---|---|
| 1 | GCC 13 и clang 18 с `-Werror`, clang-tidy, clang-format, eslint, tsc strict | 0 ошибок, 0 предупреждений |
| 2 | Тесты ×2, ASan/UBSan | C++ 55/55 (каждый дважды), web 25/25 (дважды); находок санитайзеров нет |
| 3 | Покрытие | C++ 85,5% строк (порог 70), web 100% строк (порог 70) |
| 4 | Шаги CI локально | все зелёные; на GitHub не запускался — нет удалённого репозитория |
| 5 | `docker build --no-cache` / compose | 86 с (цель ≤ 240 с); `/healthz` → 200, `/` → 200 |
| 6 | Критерии F этапа | F-требований в этапе 0 нет; F6 частично — изоляция демо-стадий в `FakeDomainService` покрыта тестом |
| 7 | Соответствие АРХ, циклы | `deps-check.sh` OK; отклонения — ADR 0007–0011 |
| 8 | Документация | README всех модулей, CLAUDE.md, этот план |
| 9 | gitleaks, авторство | утечек нет; соавторства и упоминаний ИИ в `git log` нет |

## 4. Этапы 1–4

Состав — см. постановку; детализация добавляется в этот файл в начале каждого этапа.

| Этап | Суть | Статус |
|---|---|---|
| 0 | Каркас | ✔ завершён, ворота зелёные |
| 1 | Проверка документа на демо-снапшоте (F1 точный, F2, F3, F4) | ожидает команды |
| 2 | Обновление и уведомления (F5, F6) | ожидает команды |
| 3 | Нечёткий поиск и надёжность | ожидает команды |
| 4 | Should: F7–F10 | по отдельной команде |
