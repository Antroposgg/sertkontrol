# План реализации Сертконтроля

Источники истины:

- [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) (далее — АРХ) — стек, структура, контракты C1–C9, алгоритмы, CI;
- [`docs/CASE_REQUIREMENTS.md`](CASE_REQUIREMENTS.md) (далее — КЕЙС) — ограничения и формат сдачи. При конфликте КЕЙС важнее.

Работа идёт этапами 0–4. Этап начинается только по команде, заканчивается ворота-проверкой (см. [`CLAUDE.md`](../CLAUDE.md#ворота-проверка)).

**Текущий этап: 2 — завершён 2026-09-28: ворота зелёные, кроме п. 5 (Docker) — не выполнялся в среде этапа, см. §6.4. Следующий — 3, по команде.**

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
| `fake::canonicalize/parse/key_hash/find_numbers` (упрощённые правила, FNV-1a вместо XXH3) | `libs/contracts/fakes` | `libs/canon` | 1 ✔ заменено |
| `FakeSnapshot` (3 записи в памяти) | `libs/contracts/fakes` | `libs/snapshot` (формат v1, mmap) | 1 ✔ (остаётся тестовым дублем) |
| `fake::check` (только точное совпадение, одно правило статуса) | `libs/contracts/fakes` | `libs/verify` | 1 ✔ заменено |
| `fake::recognize` (заранее заданный ответ) | `libs/contracts/fakes` | `libs/recog` | 1 ✔ заменено |
| `FakeDomainService` (в памяти) | `apps/certd/fake_domain.*` | `DomainServiceImpl` (PG + снапшот) | 1 ✔ (остаётся для тестов бота) |
| `FakeSource` | `apps/ingest/fake_source.hpp` | Демо-адаптер `data/demo` ✔, затем адаптер набора ФСА | 1 ✔ / после подтверждения данных |
| `ingest --demo / --once / --daemon` пишут «не реализовано» и выходят с кодом 0 | `apps/ingest/main.cpp` | Реальные режимы | `--demo` ✔ 1 (пара N/N+1 — 2 ✔); `--daemon` ✔ 2; `--once` — после подтверждения данных |
| `/healthz` не требует снапшот | `apps/certd/health.cpp` (`snapshot_required=false`) | Требовать загруженный снапшот | 1 ✔ |

---

### Результат ворот этапа 0 (чистый клон, `scripts/gate.sh`)

| # | Пункт | Результат |
|---|---|---|
| 1 | GCC 13 и clang 18 с `-Werror`, clang-tidy, clang-format, eslint, tsc strict | 0 ошибок, 0 предупреждений |
| 2 | Тесты ×2, ASan/UBSan | C++ 55/55 (каждый дважды), web 25/25 (дважды); находок санитайзеров нет |
| 3 | Покрытие | C++ 89,4% строк (3386/3788); web 99,31% строк |
| 4 | Шаги CI локально | все зелёные; на GitHub не запускался — нет удалённого репозитория |
| 5 | `docker build --no-cache` / compose | 94 с на чистом клоне (порог 240 с); CI #20 (PR #3) — все 11 задач зелёные. При просадке канала стенда до 0,3–0,8 МБ/с было 294 с — время почти целиком уходит на загрузку пакетов apt. `compose-smoke` → `/healthz` 200, `/` 200, согласие 204, `/api/v1/check` → `ok` |
| 6 | Критерии F этапа | F-требований в этапе 0 нет; F6 частично — изоляция демо-стадий в `FakeDomainService` покрыта тестом |
| 7 | Соответствие АРХ, циклы | `deps-check.sh` OK; отклонения — ADR 0007–0011 |
| 8 | Документация | README всех модулей, CLAUDE.md, этот план |
| 9 | gitleaks, авторство | утечек нет; соавторства и упоминаний ИИ в `git log` нет |

## 4. Этапы 1–4

Состав — см. постановку; детализация добавляется в этот файл в начале каждого этапа.

| Этап | Суть | Статус |
|---|---|---|
| 0 | Каркас | ✔ завершён, ворота зелёные |
| 1 | Проверка документа на демо-снапшоте (F1 точный, F2, F3, F4) | ✔ завершён 2026-09-27, ворота зелёные |
| 2 | Обновление и уведомления (F5, F6) | ✔ завершён 2026-09-28; Docker-пункт закрыт на этапе 3 (§7.4) |
| 3 | Нечёткий поиск и надёжность | ✔ завершён 2026-09-29; ворота зелёные (§7.4), CI зелёный |
| 4 | Should: F10, F8, F9 | ✔ 2026-09-30, ворота зелёные (§8.4); F7 (OCR фото) не делаем — решение команды 2026-09-30 |

---

## 5. Этап 1 — проверка документа на демо-снапшоте (детальный план)

Цель: номер текстом или PDF-выписка → карточка вердикта по демо-снапшоту → «на контроль» — в боте и в мини-приложении.

### 5.1. Факты MAX API, сверенные с официальными источниками (26.09.2026)

Источники: dev.max.ru/docs-api, dev.max.ru/docs/webapps/*, официальный клиент `max-messenger/max-bot-api-client-go` (`schema.yaml` — OpenAPI MAX, коммит 9f0a295).

| Факт | Значение | Влияние на этап |
|---|---|---|
| Базовый URL | `https://platform-api2.max.ru` (`platform-api.max.ru` устарел) | `MAX_API_BASE_URL` по умолчанию |
| Авторизация | Заголовок `Authorization: <token>`; query-параметр не поддерживается | `libs/maxapi` |
| TLS API | Сертификат `*.max.ru` выпущен **Russian Trusted Sub CA (Минцифры)**; в `ca-certificates` Ubuntu корня нет — проверено `openssl`/`curl` (ошибка 20) | Корень Минцифры в образе — [ADR-0012](adr/0012-mintsifry-root-ca.md) |
| Webhook | `POST /subscriptions {url, update_types, secret}`; только HTTPS:443; ответ 200 ≤ 30 с; до 10 повторов с экспоненциальной паузой; секрет 5–256 символов `[A-Za-z0-9_-]` в заголовке `X-Max-Bot-Api-Secret` | Проверка секрета, дедупликация |
| События | `update_type`: `message_created` (`message.body.mid`, `.text`, `.attachments`, `message.sender.user_id`), `message_callback` (`callback.callback_id`, `callback.payload`, `callback.user`), `bot_started` (`user`, `chat_id`, `payload`) | Ключи дедупликации `m:<mid>`, `c:<callback_id>`, `s:<user_id>:<timestamp>` |
| Отправка | `POST /messages?user_id=` с `NewMessageBody {text ≤ 4000, attachments, format: markdown|html, notify}`; ≤ 2 сообщения/с в один диалог | Карточки — `format: html`, только `<b>` + экранирование |
| Кнопки | `inline_keyboard`: ≤ 30 рядов, ≤ 7 кнопок в ряду, ≤ 3 для `link`/`open_app`; `callback.payload` ≤ **1024** символа; `open_app` требует `web_app` (публичное имя бота), `payload` `^[\w-]{0,512}$` | Payload `<действие>:<id>`; username для `open_app` — из `GET /me` (с этапа 3; `MAX_BOT_USERNAME` — запасной) |
| Ответ на кнопку | `POST /answers?callback_id=` `{message?, notification?}` | Короткое уведомление «Добавлено на контроль» |
| Вложение-файл | `{type: file, payload: {url, token}, filename, size}` | Проверка `size ≤ 20 МБ` до скачивания |
| Скачивание по `payload.url` | Нужна ли авторизация и срок жизни ссылки — **в документации не указано** | За интерфейсом `AttachmentFetcher`; в «известных ограничениях»; ручная проверка |
| MAX Bridge | `<script src="https://st.max.ru/js/max-web-app.js">`, `window.WebApp.initData`, `.platform`, `.initDataUnsafe.start_param` | Подключение в `web/index.html` |
| initData | Разбить по `&`, ровно один `hash`, URL-декодировать значения, сортировать по ключу, `k=v` через `\n`; `secret = HMAC_SHA256(key="WebAppData", msg=BOT_TOKEN)`; `hash = hex(HMAC_SHA256(secret, строка))`; `auth_date` — секунды | `libs/maxapi/init_data`; тест-вектор посчитан независимой реализацией по официальному алгоритму |
| `@maxhub/max-ui` | MIT, peer `react 19.2.8` | React закреплён на 19.2.8 |

### 5.2. Решения этапа

| # | Решение | Почему |
|---|---|---|
| 1 | Формат снапшота v1 — `docs/snapshot-format.md`; writer этапа 1 держит записи в памяти | Демо — десятки записей. Потоковая сборка с сортировкой перестановки (АРХ §6) — вместе с адаптером настоящего набора, когда источник подтверждён |
| 2 | `ingest --demo` собирает только снапшот N (версия 1) из `data/demo/base.tsv`, регистрирует его в `snapshot_version` (C8) через libpq | N+1, `demo_stage` и `NOTIFY` — этап 2 |
| 3 | `certd` при старте берёт последнюю `ready`-версию из `snapshot_version` и повторяет попытку раз в 5 с, пока снапшота нет; в compose `certd` стартует после завершения `ingest` | `LISTEN snapshot_ready` и горячая замена — этап 2 |
| 4 | Распознавание — только PDF (QR → текстовый слой) в отдельном пуле потоков; фото → `415` с пояснением | OCR и фото — F7, этап 4 |
| 5 | Ссылка на реестр: из QR, иначе по `registry_id` (`https://pub.fsa.gov.ru/rds/declaration/view/<id>/common`, `…/rss/certificate/view/<id>/common`); при `registry_id = 0` — страница поиска реестра | Для вымышленных демо-записей не ведём на чужую реальную запись |
| 6 | Callback-кнопка «На контроль» несёт id строки `check_log` (`w:<id>`), «Снять» — id `portfolio_item` (`d:<id>`); сервер сверяет владельца | АРХ §8: аргумент — id записи в БД; IDOR-защита |
| 7 | Исходящие сообщения — через таблицу `outbox` (C9) и простой отправитель (последовательно, 3 попытки); ответы на кнопки (`/answers`) — напрямую | Двухуровневый token bucket и повторы с джиттером — этап 2 |
| 8 | Локальная авторизация мини-приложения без MAX: `CERTD_DEV_USER_ID` (в compose — `1000001`) работает, только если `MAX_BOT_TOKEN` пуст; с токеном игнорируется | [ADR-0013](adr/0013-dev-auth-without-max.md): жюри открывает мини-приложение в браузере; в проде режим невозможен |
| 11 | `gcc-release` собирается unity-сборкой, как образ; тестовые цели — без unity | Конфликты имён из анонимных пространств ловятся до `docker build` |
| 9 | Лимит проверок — 30 в минуту на пользователя (в памяти процесса) | АРХ §10 «Перегрузка проверками» |
| 10 | Тесты БД-слоя — на настоящем PostgreSQL 16: `scripts/ci/with-pg.sh` поднимает временный контейнер для всех C++-прогонов | F4 и IDOR проверяются на реальных запросах |

### 5.3. Работы

| # | Модуль | Результат | F | Тест |
|---|---|---|---|---|
| 1.1 | `libs/canon` | `canonicalize` (UTF-8, гомоглифы, тире, `№`), грамматика, `key_hash` = XXH3, `find_numbers` (до 20, без дублей), `kVersion = 1` | F1 | `tests/canon/golden_test` + `tests/canon_golden.tsv` (первые 3 строки — АРХ §7.1) |
| 1.2 | `libs/snapshot` | Формат v1: заголовок 256 байт, секции по 64 байта, `PackedRecord` 64 байта, `serial_index`, строки; writer (`.tmp` → `fsync` → `rename`), reader (`mmap` + `memcpy`, проверка заголовка, границ и XXH3), `SnapshotHolder` | F1 | roundtrip, битые файлы, `by_serial`, holder |
| 1.3 | `libs/verify` | Точный поиск (`equal_range` + сравнение строки), правила статуса и срока, метки «факт / расчёт / рекомендация», «нет в данных на <дату>» + до 3 ближайших номеров (та же серия и год) | F1, F3 | `tests/verify/rules_test` (табличный, по `docs/rules.md`) |
| 1.4 | `data/demo`, `apps/ingest` | `base.tsv` (вымышленные записи + 89369/26), адаптер C3 `DemoTsvSource`, `ingest --demo` → файл + `snapshot_version` | F1 | адаптер, детерминированность сборки |
| 1.5 | `libs/recog` | PDF: лимиты (20 МБ, 10 страниц, 25 Мпикс, 15 с), QR со страниц с конца (poppler + zxing), текстовый слой | F2 | фикстура `tests/fixtures/extract-89369-26.pdf` (сгенерирована из `.tex`, данные вымышленные, номер из АРХ) |
| 1.6 | `libs/maxapi` | initData, разбор `Update`, сборка `NewMessageBody`, клиент Bot API (Drogon `HttpClient`) | — | вектор initData, JSON-фикстуры событий |
| 1.7 | `apps/certd` | `DomainServiceImpl` (PG + снапшот + пул распознавания), REST `/api/v1` (RFC 9457, initData, фильтр `user_id`), webhook (секрет, дедупликация), outbox-отправитель, загрузка снапшота | F1–F4 | REST на `FakeDomainService`, домен на PG (IDOR «чужой id → 404»), webhook |
| 1.8 | `apps/certd/bot` | Приветствие и согласие, карточка (факт / расчёт / рекомендация, дата данных, «тестовые данные»), сводка при > 3 номерах, «На контроль», «Снять с контроля» | F3, F4 | рендер карточки и клавиатур, сценарии на фейках |
| 1.9 | `openapi.yaml` | C7: `/me`, `/check`, `/check/file`, `/portfolio` (GET/POST/DELETE), `/data-status`, `/healthz`, `/max/webhook` | — | разбор YAML и соответствие путей обработчикам |
| 1.10 | `web/` | MAX Bridge, `@maxhub/max-ui`, экраны «Портфель» (фильтры, снятие) и «Добавить» (номер, файл), карточка вердикта; состояния загрузка / пусто / ошибка | F3, F4 | Vitest |
| 1.11 | Docker, compose, CI | Корень Минцифры, `ingest --demo` при старте, `with-pg.sh` в CI | — | compose-smoke: `/healthz` со снапшотом, `/api/v1/check` |

### 5.4. Заглушки этапа 1

| Заглушка | Где | Заменяется | Этап |
|---|---|---|---|
| Ближайшие номера — только та же серия/год, обычное расстояние Левенштейна; вопрос «Это номер …?» не задаётся | `libs/verify` | Взвешенный Левенштейн, варианты серии, пороги, подтверждение | 3 |
| Отправитель outbox без token bucket и джиттера | `apps/certd/outbox_sender` | Двухуровневый лимитер (АРХ §7.6) | 2 ✔ |
| Загрузка снапшота при старте + опрос раз в 5 с | `apps/certd/snapshot_loader` | `LISTEN snapshot_ready` и swap | 2 ✔ |
| `simulate_update` / `reset_demo` возвращают `forbidden` | `DomainServiceImpl` | Демо-режим N/N+1 | 2 ✔ |
| Фото → 415 | `libs/recog` | OCR (F7), QR с камеры (F10) | 4 |
| Кнопка «Указать поставщика» в боте отсутствует (ИНН поставщика — в мини-приложении) | `apps/certd/bot` | Диалог ввода ИНН | 2 ✔ |
| Экран «Документ» отсутствует; «Данные» — без демо-кнопки | `web/` | Карточка с историей, «Симулировать обновление» | 2 ✔ |
| Пагинация портфеля в интерфейсе («Показать ещё») | `web/src/screens/Portfolio.tsx` | Кнопка по `next_cursor` | 2 ✔ |

### 5.5. Результат ворот этапа 1 (чистый клон, те же скрипты, что в CI)

| # | Пункт | Результат |
|---|---|---|
| 1 | GCC 13 (unity) и clang 18 с `-Werror`, clang-tidy, clang-format, eslint, tsc strict | 0 ошибок, 0 предупреждений |
| 2 | Тесты ×2, ASan/UBSan | C++ 173/173 (каждый дважды, с PostgreSQL 16); web 41/41 (дважды); санитайзеры без находок |
| 3 | Покрытие | C++ 91,2% строк (2547/2792); web 99,45% строк |
| 4 | Шаги CI локально | все зелёные |
| 5 | `docker build --no-cache` / compose | 93 с; `/healthz` → 200 со снапшотом v1, `/api/v1/check` → вердикт `ok` по демо-данным |
| 6 | Критерии F этапа | таблица 5.6 |
| 7 | Соответствие АРХ, циклы | `deps-check.sh` OK; отклонения — ADR 0012, 0013 |
| 8 | Документация | README модулей, CLAUDE.md, этот план, `openapi.yaml`, `docs/rules.md`, `docs/snapshot-format.md` |
| 9 | gitleaks, авторство | утечек нет; соавторства и упоминаний ИИ в `git log` нет |

### 5.6. F-требования этапа 1 → код → тест

| F | Критерий приёмки (АРХ §2) | Код | Тест |
|---|---|---|---|
| F1 (точный) | Номер в любой раскладке, до 20 в сообщении; ответ ≤ 1 с | `libs/canon`, `libs/verify/src/verify.cpp`, `DomainServiceImpl::check_text` | `CanonGolden.*`, `FindNumbers.*`, `RulesTest.Table`, `DomainPgTest.CheckTextLogsEveryNumber`, `BotTest.ManyNumbersGiveSummaryAndWatchAll` |
| F2 | Выписка 89369/26 → верный номер и ссылка на реестр | `libs/recog`, `DomainServiceImpl::check_file`, `Bot::on_message` | `Recognize.ExtractGivesNumberAndRegistryLink` (34 мс), `DomainPgTest.CheckFileUsesQrLink`, `BotTest.PdfAttachmentFlow`, `RestApiTest.CheckFile` |
| F3 | Все поля и метки факт / расчёт / рекомендация; «нет в данных на <дату>» и ближайшие номера | `libs/verify`, `bot/card.cpp`, `web/src/components/VerdictCard.tsx` | `RulesTest.*`, `BotTest.TextGivesVerdictCard`, `BotTest.ProblemCardHasCalculationAndRecommendation`, web `Add.test.tsx` |
| F4 | Портфель: на контроль, список с фильтрами, удалить — из бота и мини-приложения | `DomainServiceImpl`, `rest_api.cpp`, `bot/bot.cpp`, `web/src/screens/Portfolio.tsx` | `DomainPgTest.PortfolioLifecycle`, `DomainPgTest.IdorForeignIdIsNotFound`, `RestApiTest.PortfolioLifecycleAndFilters`, `BotTest.WatchAndUnwatch`, web `Portfolio.test.tsx` |

Время ответа F1: вердикт по снапшоту — микросекунды, ответ REST в тестах — единицы миллисекунд; p95 на
настоящем MAX (webhook → отправка) измеряется после подключения бота — пункт ручной проверки.

---

## 6. Этап 2 — обновление данных и уведомления (детальный план)

Цель: новая версия данных → ровно одно уведомление владельцу изменившегося документа; демо-сценарий жюри
(N → «Симулировать обновление» → N+1 → уведомление → «Сбросить демо») одним аккаунтом.

### 6.1. Решения этапа

| # | Решение | Почему |
|---|---|---|
| 1 | Демо-пара — фиксированные версии 1 (N, `demo_stage = 0`) и 2 (N+1, `demo_stage = 1`); роль версии — колонка `snapshot_version.demo_stage` (миграция `0003`) | `certd` держит N и N+1 открытыми одновременно и выбирает по стадии пользователя, а не по номеру версии |
| 2 | `registry_change`, перевод версии в `ready` и `pg_notify('snapshot_ready', v)` — одна транзакция | Версия не бывает видна без своих изменений; NOTIFY доставляется только после COMMIT |
| 3 | diff — merge-join двух снапшотов в порядке (`key_hash`, канонический номер), O(n + m), без хеш-таблиц | Оба снапшота уже отсортированы (формат v1); память O(1) сверх изменений |
| 4 | `notify_changes(v)` сравнивает `portfolio_item.last_status` со снапшотом, а не только с `registry_change` | Закрывает гонку «документ добавлен во время сборки»; пропущенные версии сворачиваются в одно сообщение |
| 5 | Пачка `notify_changes` — одна SQL-инструкция (CTE: `UPDATE … WHERE last_version < v` → `INSERT notification ON CONFLICT DO NOTHING` → `INSERT outbox` только для реально вставленных) | Атомарность без явной транзакции в корутине; повтор после падения и два исполнителя не дублируют сообщение |
| 6 | Очередь задач — таблица `job` (ADR-0005): аренда `FOR UPDATE SKIP LOCKED` на 5 мин, backoff min(30 с·2^(n−1), 1 ч), 5 попыток, затем `run_at = infinity` с `last_error` | Задача переживает рестарт; зависшая аренда истекает сама |
| 7 | На каждую боевую версию — две задачи: `v:1` сразу и `v:2` через 10 мин | Второй проход подбирает документы, поставленные на контроль в окно сборки |
| 8 | Отправитель outbox — `SendLimiter` (глобально C = 5, r = 25/с; чат C = 1, r = 1/с); нет токена чата — все сообщения чата откладываются без траты попытки; 429/5xx — повтор через min(2^(n−1) с, 5 мин)·U(0,5; 1), 429 обнуляет глобальное ведро; 8 попыток | АРХ §7.6: в любом окне τ ≤ C + r·τ вызовов — 30/с и 2/с в чат, ровно лимиты MAX |
| 9 | «Симулировать обновление» синхронно: `demo_stage` 0 → 1 и `notify_user` против N+1; ответ `200 {notified}`; история — `GET /history?number=`; `POST /demo/reset` | [ADR-0014](adr/0014-history-and-demo-endpoints.md) |
| 10 | Демо-пользователь не видит боевых данных и наоборот: `SnapshotSet::for_user` выбирает по `is_demo` и стадии; `simulate_update` боевому — `403` | КЕЙС §2 п.10: тестовые данные не смешиваются с настоящими |
| 11 | Рендер уведомления — в боте (`bot::render_change_notice`), домен получает его через `NoticeRenderer` из `main.cpp` | Граф зависимостей CLAUDE.md: домен не зависит от бота |
| 12 | Диалог «Указать поставщика» — `dialog_state` с TTL 30 мин; не-цифры или вложение закрывают диалог и обрабатываются как обычное сообщение | Пользователь не «застревает» в диалоге, номер документа в ответ не теряется |
| 13 | `ingest --daemon` — ежедневно в 04:00 МСК (UTC+3 без переходов), остановка по SIGTERM за ≤ 1 с; `--once` пишет, что источник не подтверждён | Адаптер набора ФСА — после подтверждения данных (АРХ §1, главный риск) |
| 14 | Очистка по срокам хранения (АРХ §10) — задача `cleanup` раз в сутки: `check_log` 90 дн., `inbound_update` 7 дн., отправленный/отвергнутый outbox 30 дн., `dialog_state` 1 день | Персональные данные не копятся бессрочно |

### 6.2. Работы

| # | Модуль | Результат | F | Тест |
|---|---|---|---|---|
| 2.1 | `libs/snapshot` | `diff(before, after, sink)` → `DocChange`, `DiffStats` (в т. ч. переходы статусов); `find_index` | F5 | `DiffTest.*` (включая property «diff ≡ наивный std::map» на 150 случайных парах) |
| 2.2 | `libs/maxapi` | `TokenBucket`, `SendLimiter` | F5 | `TokenBucket.*`, `SendLimiter.*` (property «≤ C + r·τ в любом окне», 8 000 запросов) |
| 2.3 | `data/demo`, `apps/ingest`, `db` | `next.tsv` (5 изменений), `run_demo_pair`, `RegistryDb::publish`, `--daemon`; миграция `0003` | F5, F6 | `DemoPair.*` (в т. ч. `LISTEN` через libpq), `RegistryJson.*`, `Schedule.*`, `schema_test.sql` |
| 2.4 | `apps/certd` | `SnapshotSet` + `SnapshotLoader` (`LISTEN` + опрос 60 с), `JobQueue`/`JobRunner`, `NotifyService`, `OutboxSender` с лимитером, `cleanup_retention`, домен: `simulate_update`, `reset_demo`, `history`, `attach_supplier` | F5, F6 | `DomainPgTest.*`, `PortsPgTest.*`, `RestApiDemo.*` |
| 2.5 | `apps/certd/bot` | `render_change_notice` (≤ 4000 символов, одно сообщение на пользователя), диалог ИНН, кнопка `s:` | F5 | `BotTest.SupplierDialog`, `Card.*` |
| 2.6 | `openapi.yaml` | 1.1.0: `/history`, `/demo/simulate-update`, `/demo/reset`, поля `DataStatus` | F6 | `openapi_test`, `RestApiDemo.SimulateHistoryAndReset` |
| 2.7 | `web/` | «Документ» (карточка + история), демо-блок на «Данных», «Показать ещё» и «Открыть» в портфеле | F5, F6 | `Document.test.tsx`, `Data.test.tsx`, `Portfolio.test.tsx`, `App.test.tsx`, `format.test.ts` |
| 2.8 | compose, CI | `ingest-demo` (разовый) + `ingest` (демон); smoke проверяет оба | F6 | `compose-smoke.sh` |
| 2.9 | Сценарий жюри | Webhook → PDF → «На контроль» → «Симулировать обновление» → отправитель → ровно одно уведомление | F5, F6 | `JuryScenarioTest.ExtractWatchSimulateNotify` |

### 6.3. Заглушки этапа 2

| Заглушка | Где | Заменяется | Этап |
|---|---|---|---|
| `ingest --once` / ежедневный запуск не собирают боевой снапшот | `apps/ingest` | Адаптер набора ФСА (C3) | после подтверждения данных |
| Лимитер и отправитель — в памяти одного процесса `certd` | `apps/certd/pg_ports`, `libs/maxapi` | Общий лимитер в PostgreSQL при нескольких экземплярах | при масштабировании (АРХ §3) |
| «Подробнее» (`start_param = check-<id>`) открывает «Портфель», а не документ | `web/` | Эндпоинт «проверка по id» + разбор `start_param` | 3 |
| SKU в уведомлении — только если задан; ввод SKU в боте отсутствует | `apps/certd/bot` | Импорт CSV (F9) | 4 |
| Мини-приложение не спрашивает согласие на обработку данных, REST его не проверяет (в боте проверяется) — пробел этапа 1, АРХ §10 | `web/`, `apps/certd/rest_api.cpp` | Экран согласия + `POST /me/consent` (C7) и проверка в `DomainService` | 3 |

### 6.4. Результат ворот этапа 2 (те же скрипты, что в CI)

| # | Пункт | Результат |
|---|---|---|
| 1 | GCC 13 (unity) и clang 18 с `-Werror` из чистого клона, clang-tidy (все 67 файлов), clang-format, eslint, tsc strict | 0 ошибок, 0 предупреждений; сборка + тесты из чистого клона: GCC 118 с, clang ASan/UBSan 138 с |
| 2 | Тесты ×2, ASan/UBSan | C++ 203/203 (каждый дважды, с PostgreSQL 16, 0 пропущенных); web 57/57 (дважды); санитайзеры без находок |
| 3 | Покрытие | C++ 89,8% строк (3056/3402); web 99,61% строк (258/259) |
| 4 | Шаги CI локально | все, кроме Docker-шагов, зелёные; `db-migrations` — тот же сценарий на нативном PostgreSQL 16.15 (дважды + `schema_test.sql`) |
| 5 | `docker build --no-cache` / compose | **не выполнялся**: в среде этапа нельзя скачивать базовые образы. Вместо compose-smoke — нативный прогон: `ingest --demo` → `certd` → `/healthz` 200, демо-сценарий через REST (`simulate-update` → `{"notified":1}`, повтор → `0`, история, `reset` → 204), `NOTIFY` новой боевой версии → загрузка → `notify_changes` → `notification` за 1 с. Команда для закрытия пункта: `scripts/ci/docker-build.sh && scripts/ci/compose-smoke.sh` |
| 6 | Критерии F этапа | таблица 6.5; сценарий жюри целиком — `JuryScenarioTest.ExtractWatchSimulateNotify` (0,6 с) |
| 7 | Соответствие АРХ, циклы | `deps-check.sh` OK; отклонения — [ADR-0014](adr/0014-history-and-demo-endpoints.md) |
| 8 | Документация | README модулей, CLAUDE.md, этот план, `openapi.yaml` 1.1.0, `docs/rules.md`, журнал контрактов |
| 9 | gitleaks, авторство | утечек нет; соавторства и упоминаний ИИ в `git log` нет |

Найдено и исправлено попутно (пробелы этапа 1): правило V5 каталога R2 («номер текущего года мог появиться после
даты данных») не было реализовано — `not_found.recent`; журнал `certd` при запуске вне терминала буферизовался
блоками и не был виден в `docker compose logs` до остановки — включена построчная буферизация.

### 6.5. F-требования этапа 2 → код → тест

| F | Критерий приёмки (АРХ §2) | Код | Тест |
|---|---|---|---|
| F5 | Смена статуса наблюдаемого документа → ровно одно сообщение ≤ 15 мин после готовности снапшота | `libs/snapshot/src/diff.cpp`, `apps/ingest/registry_db.cpp` (`publish` + NOTIFY), `apps/certd/snapshot_loader.cpp`, `jobs.cpp`, `notify.cpp`, `pg_ports.cpp` (`OutboxSender`), `libs/maxapi/src/rate_limit.cpp`, `bot/card.cpp` (`render_change_notice`) | `DiffTest.MatchesNaiveMapOnRandomPairs`, `DemoPair.PublishesBothVersionsWithChangesAndNotify`, `PortsPgTest.NotifyVersionIdempotentAcrossCrash`, `PortsPgTest.JobQueueLeaseRetryAndExhaustion`, `PortsPgTest.OutboxSenderDefersChatOverLimit`, `SendLimiter.WindowBoundHoldsForRandomTraffic`, `Card.ChangeNoticeOneMessagePerUser`, `JuryScenarioTest.ExtractWatchSimulateNotify` |
| F6 | Демо N / N+1, «Симулировать обновление», всё помечено тестовым; проверяющий проходит сценарий одним аккаунтом за 5 минут | `data/demo/next.tsv`, `apps/ingest/demo_pair.cpp`, `apps/certd/snapshot_set.hpp`, `DomainServiceImpl::{simulate_update, reset_demo, history}`, `rest_api.cpp`, `web/src/screens/{Data,Document}.tsx` | `DomainPgTest.DemoSimulateNotifiesOnceAndResetRepeats`, `DomainPgTest.HistoryFollowsDemoStage`, `DomainPgTest.DemoForbiddenForProdUsers`, `RestApiDemo.SimulateHistoryAndReset`, web `Data.test.tsx`, `Document.test.tsx`, `JuryScenarioTest.ExtractWatchSimulateNotify` |

Оценка «≤ 15 мин» для F5: NOTIFY доходит сразу после COMMIT, при потере — опрос раз в 60 с; задача `v:1` ставится
с `run_at = now()`, исполнитель опрашивает очередь раз в секунду, отправитель — раз в 0,3 с. Худший случай при сбоях
задачи: 60 с + повторы через 30, 60, 120, 240 с = 510 с ≈ 8,5 мин < 15 мин; при исправной БД — секунды (в тесте жюри — < 1 с).
Отправка N уведомлений ограничена лимитером: 25 сообщений/с, т. е. 15 мин ≈ 22 500 пользователей с изменениями.

---

## 7. Этап 3 — нечёткий поиск и надёжность (детальный план)

Цель: F1 полностью (номер с 1–2 ошибками находится и подтверждается вопросом «Это номер …?»), доказательства
надёжности — property-тесты, fuzz, TSan, бенчмарки против целей АРХ §2, контракт C7 проверен линтером и по живому
сервису; README закрывает весь чек-лист «Формата сдачи» КЕЙСА.

### 7.1. Решения этапа

| # | Решение | Почему |
|---|---|---|
| 1 | Взвешенный Левенштейн по кодовым точкам: замена из таблицы путаницы OCR (O↔0, B↔8, S↔5, I↔1, Z↔2) — 0,3, прочие операции — 1 | АРХ §7.2; по кодовым точкам — кириллица в теле номера (`AЯ46`) считается одной правкой |
| 2 | Кандидаты: `serial_index` по серии и году запроса + варианты серии с одной заменой цифры (длина × 9) + серия, где буквы-двойники цифр (O, B, S, I, Z) заменены цифрами | Дешевле BK-дерева (АРХ §7.2); OCR путает цифры и буквы именно в серии |
| 3 | Порог: лучший кандидат с расстоянием ≤ 2 и отрывом от второго ≥ 0,5 → `needs_confirmation` («Это номер …?»); иначе `not_found` и до 3 ближайших | АРХ §7.2: любое ненулевое расстояние — вопрос, а не молчаливая подмена |
| 4 | «Да» подтверждает номер из `check_log` пользователя: `DomainService::confirm(check_id)` перепроверяет подсказку и пишет новую проверку; кнопки `y:<check_id>` / `n:<check_id>` | АРХ §8 (payload `y:`/`n:`); аргумент — id БД, владелец проверяется доменом |
| 5 | Fuzz — отдельный пресет `fuzz` (clang, `-fsanitize=fuzzer,address,undefined`), цели в `fuzz/`, стартовые корпуса в `fuzz/corpus/<цель>`, smoke 60 с на цель в CI (матрица) | АРХ §10 |
| 6 | TSan — пресет `clang-tsan`, отдельный тест `concurrency_test` (замена снапшота под нагрузкой читателей, лимитер) без Drogon/libpq; пул распознавания в него не входит — он построен на `trantor::EventLoopThreadPool`, а неинструментированный trantor дал бы ложные срабатывания; пул покрыт `certd_test` под ASan | Неинструментированные библиотеки дают ложные срабатывания; гонки нашего кода проверяются полностью |
| 7 | Бенчмарки — Google Benchmark на синтетическом снапшоте (по умолчанию 1 млн записей), перцентили p50/p99 считаются счётчиками; `scripts/ci/bench.sh` сверяет p99 с целями | АРХ §2: точный ≤ 1 мс, нечёткий ≤ 5 мс |
| 8 | Линтер OpenAPI — `@redocly/cli lint` (npm, версия закреплена); контрактные тесты — schemathesis 4.28.0 (Docker-образ) против compose-стека в боевом режиме: тестовый токен, initData подписана по алгоритму MAX, dev-вход выключен; проверка `unsupported_method` заменена своей (405 + `Allow` на PUT/PATCH, ADR-0016) | АРХ §10 «Интеграция»; ни одной C++-зависимости не добавляется; dev-режим прятал бы проверку авторизации |
| 9 | Долги этапа 2: согласие в мини-приложении (`POST /me/consent`, REST проверяет согласие), «Подробнее» (`start_param`) открывает документ | docs/plan.md §6.3 |

### 7.2. Работы

| # | Модуль | Результат | Тест |
|---|---|---|---|
| 3.1 | `libs/verify` | `weighted_distance`, `fuzzy_match` (кандидаты, порог, отрыв), правило `fuzzy.confirm` | `FuzzyTest.*`, property «нечёткий ≡ перебор» на 10 000 записей |
| 3.2 | `apps/certd`, `bot`, `web` | `confirm(check_id)` (C6), кнопки `y:`/`n:`, «Это номер …?» в мини-приложении | `BotTest.*`, `DomainPgTest.*`, `Add.test.tsx` |
| 3.3 | `fuzz/` | цели: `canon`, `snapshot_reader`, `update_json`, `init_data`, `demo_tsv`, `outbox_json`, `weighted_distance` | smoke 60 с каждая |
| 3.4 | `tests/concurrency` | замена снапшотов под нагрузкой, лимитер | TSan без находок |
| 3.5 | `bench/` | точный/нечёткий поиск, канонизация, diff, сборка снапшота | `scripts/ci/bench.sh` против целей АРХ §2 |
| 3.6 | `openapi.yaml`, CI | линтер, schemathesis в compose | зелёные в CI |
| 3.7 | долги этапа 2 | согласие в мини-приложении, `start_param` | `RestApi*`, `App.test.tsx` |
| 3.8 | `README.md` | все пункты «Формата сдачи» КЕЙС §4 | чек-лист |

### 7.3. Заглушки и отложенное после этапа 3

| Что | Сейчас | Когда |
|---|---|---|
| OCR фото (F7) | `recog` отвечает «пришлите PDF»; пакеты Tesseract не ставятся в образ ([ADR-0017](adr/0017-tesseract-with-ocr-stage.md)); QR выписки читается сканером MAX (F10) | не делаем (решение 2026-09-30) |
| Импорт CSV (F9), `openCodeReader` (F10), SKU в мини-приложении | нет | этап 4 |
| Источник ФСА (`ingest --once`) | демо-данные N / N+1 | после подтверждения данных |
| `TRACE` → 405 без `Allow` | ограничение Drogon 1.8.7, ADR-0016 | — |

### 7.4. Результат ворот этапа 3 (чистый клон, `scripts/gate.sh`)

| # | Пункт | Результат |
|---|---|---|
| 1 | GCC 13 (unity) и clang 18 с `-Werror`, clang-tidy (72 файла), clang-format, ESLint, tsc strict, линтер OpenAPI (@redocly/cli) | 0 ошибок, 0 предупреждений |
| 2 | Тесты ×2, ASan/UBSan, TSan | C++ 230/230 (каждый дважды, с PostgreSQL 16, 0 пропущенных — и на хосте, и в контейнере ворот); web 65/65 (дважды); ASan/UBSan без находок; TSan (`concurrency_test`, 2 теста) без находок |
| 3 | Покрытие | C++ 89,3% строк (3348/3749); web 99,31% строк |
| 4 | Шаги CI локально | все зелёные; новые: TSan, fuzz smoke (7 целей × 60 с), бенчмарки, линтер OpenAPI, контрактные тесты schemathesis |
| 5 | `docker build --no-cache` / compose | CI (раннер GitHub, PR #3, запуск 17): вся задача Docker — сборка `--no-cache`, compose-smoke и контрактные тесты — 1 мин 36 с, порог 240 с выполнен. На стенде разработки — 294 с из-за канала 0,3–0,8 МБ/с (apt 259 с, компиляция 25 с); `compose-smoke` → `/healthz` 200, `/` 200, согласие 204, `/api/v1/check` → `ok` |
| 6 | Критерии F этапа | таблица 7.5 |
| 7 | Соответствие АРХ, циклы | `deps-check.sh` OK; отклонения — ADR-0015 (libcurl), [ADR-0016](adr/0016-trace-405-without-allow.md) (`TRACE`), [ADR-0017](adr/0017-tesseract-with-ocr-stage.md) (Tesseract — на этапе 4) |
| 8 | Документация | корневой README по КЕЙС §4, README модулей, CLAUDE.md, этот план, `openapi.yaml` 1.3.0, `docs/rules.md`, журнал контрактов |
| 9 | gitleaks, авторство | утечек нет; соавторства и упоминаний ИИ в `git log` нет |

Бенчмарки (1 млн записей, `scripts/ci/bench.sh`): точный поиск p50 1,5 мкс / p99 3,3 мкс (цель ≤ 1 мс), нечёткий
p50 135 мкс / p99 289 мкс (цель ≤ 5 мс), «нет в данных» p99 10 мкс, diff 78 мс.

Найдено и исправлено воротами этапа:
- проверка в MAX (коллеги): бот не отвечал на номер, кнопки «Согласен» и «Как это работает» не срабатывали — в
  `MAX_BOT_USERNAME` было отображаемое имя бота, и MAX отвергал каждое сообщение с кнопкой `open_app` (HTTP 404
  `Link not found`). Username теперь берётся из `GET /me` (`bot_identity.hpp`), переменная — запасное значение;
  набранные `/start` и `/help` отвечают справкой (раньше — «Не нашёл номер»). В мини-приложении статус в заголовке
  карточки слипался с номером («…/26действует»).
- fuzz: jsoncpp бросает `Json::RuntimeError` на вложенности глубже `stackLimit` — разбор в `maxapi` и тело
  `POST /portfolio` теперь дают ошибку данных (раньше REST отвечал 500); ридер снапшота принимал пустую секцию со
  смещением за файлом (UB при `base + offset`). Входы сохранены в корпусах как `regression-*`, добавлены юнит-тесты.
- schemathesis: неизвестные и пустые параметры запроса принимались молча; `PUT`/`GET` на `/portfolio/{id}` давали 404
  вместо 405 (Drogon 1.8.7 на маршрутах с параметром).
- ворота: dev-образ не собирался с нуля (`.dockerignore` исключал `scripts/`, нужный `dev.Dockerfile`) — у него свой
  `docker/dev.Dockerfile.dockerignore`, тег образа — хеш входных файлов; в dev-контейнере не было Docker, и `with-pg.sh` пропускал тесты с БД, а провал `ASSERT` в `SetUpTestSuite`
  gtest печатает как SKIPPED — ctest засчитывал такие тесты пройденными. Теперь база поднимается на хосте
  (`SK_REQUIRE_PG=1` делает пропуск ошибкой, и в CI тоже), фикстуры проверяют готовность в `SetUp`, в dev-образ
  добавлен CLI `openssl` (TlsTest). gcov давал отрицательные счётчики на многопоточных тестах — `-fprofile-update=atomic`.

### 7.5. F-требования этапа 3 → код → тест

| F | Критерий приёмки (АРХ §2) | Код | Тест |
|---|---|---|---|
| F1 (полностью) | Номер с 1–2 ошибками распознавания находится; любое ненулевое расстояние — вопрос «Это номер …?», а не подмена | `libs/verify/src/fuzzy.cpp` (`weighted_distance`, `fuzzy_match`), `verify.cpp` (`needs_confirmation`), `DomainServiceImpl::confirm`, `bot/card.cpp` (`confirm_question`), `bot.cpp` (`y:`/`n:`), `web/src/screens/Add.tsx` («Да, проверить») | `WeightedDistance.Costs`, `FuzzyTest.OcrStringFromArchitecture`, `FuzzyTest.DistortedSerialFoundByVariants`, `FuzzyTest.AmbiguousOrFarIsNotConfident`, `FuzzyTest.ExactMatchIsNotFuzzy`, `FuzzyProperty.MatchesBruteForceOn10kRecords`, `DomainPgTest.FuzzyNumberConfirmed`, `BotTest.ConfirmSuggestedNumber`, `Card.NeedsConfirmationAsksQuestion`, web `Add.test.tsx` «подсказка → «Да, проверить» → карточка» |
| F1 (скорость) | Точный ≤ 1 мс, нечёткий ≤ 5 мс (p99) | `libs/snapshot` (mmap, `serial_index`), `libs/verify` | `bench/search_bench.cpp` + `scripts/ci/bench.sh` (порог в CI) |
| Долги этапа 2 | Согласие в мини-приложении; «Подробнее» открывает документ | `rest_api.cpp` (`authorize`, `consent`), `web/src/components/ConsentGate.tsx`, `bot/card.cpp` (`document_start_param`), `web/src/max/bridge.ts` (`getStartTarget`) | `RestApiTest.ConsentRequired`, web `ConsentGate.test.tsx`, `Card.DocumentStartParamFitsMaxLimits`, web `App.test.tsx` «Подробнее из бота открывает документ», `bridge.test.ts` |
| Проверка в MAX | Кнопки и карточки бота доходят до пользователя; `/start` — справка; статус в заголовке карточки web | `bot_identity.cpp` (`GET /me`), `maxapi` `get_me`/`parse_bot_info`, `bot.cpp` (`split_bot_command`), `web/src/components/VerdictCard.tsx` | `BotIdentity.UsernameFromGetMe`, `HttpBotApiTest.GetMeReturnsUsername`, `BotInfo.Parse`, `BotTest.StartAndHelpCommands`, `BotTest.StartCommandWithoutConsentAsksForIt`, web `Add.test.tsx`, fuzz `fuzz_update_json` |
| Надёжность (АРХ §10) | fuzz, TSan, контракт C7 | `fuzz/`, `tests/concurrency/`, `openapi.yaml`, `scripts/ci/contract-test.sh` | fuzz smoke 7 × 60 с, `SnapshotSwap.*`, `RateLimiterConcurrency.*`, schemathesis (все проверки) + 405/`Allow` |

---

## 8. Этап 4 — Should: F10, F8, F9, затем F7 (детальный план)

Порядок — по отдаче и риску (команда пользователя 2026-09-29): F10 (платформенный бонус КЕЙС §5.4) → F8 → F9;
F7 (OCR фото) — отдельным шагом после них.

### 8.1. Факты MAX, сверенные с официальными источниками (29.09.2026)

| Что | Факт | Источник |
|---|---|---|
| Сканер QR | `window.WebApp.openCodeReader(fileSelect = true)` → `Promise<string>` с содержимым кода; `fileSelect=false` — только камера | dev.max.ru/docs/webapps/bridge |
| Поддержка | Только iOS и Android; «not supported on desktop and web clients»; платформа — `WebApp.platform` (`ios`, `android`, `desktop`, `web`) | там же |
| QR выписки ФСА | Ссылка на запись реестра `https://pub.fsa.gov.ru/rds/declaration/view/<id>/common` (сертификаты — `/rss/certificate/view/<id>/…`), номера документа в QR нет | фикстура `extract-89369-26` (АРХ §1, спайк); формат ссылки — `docs/rules.md` |

### 8.2. Решения этапа

| # | Решение | Почему |
|---|---|---|
| 1 | F10: кнопка «Сканировать QR» на экране «Добавить» только при `platform ∈ {ios, android}` и наличии `openCodeReader`; текст QR уходит в тот же `GET /check?number=` | На вебе метод не поддержан (АРХ §2 F10: «кнопка скрыта, работает загрузка файла») |
| 2 | Ссылка на запись реестра в тексте запроса (из QR, вставленная в бот или в «Добавить») находится по ID записи: `verify::parse_registry_url` + новый метод C2 `Snapshot::by_registry_id` | В QR выписки нет номера — только ID; открытый вопрос АРХ §11 «индекс по registry_id» |
| 3 | `by_registry_id` в ридере — отсортированный индекс в памяти, строится один раз при первом вызове (`std::call_once`); формат файла не меняется | Бинарный поиск вместо прохода по 1 млн записей; снапшоты без обращений по ID не платят ни памятью, ни временем загрузки |
| 4 | F8: `verify::Query::supplier_inn` (C4); правила `supplier.match` / `supplier.mismatch` / `supplier.unknown` (расчёт) и `advice.check_supplier` (рекомендация); несовпадение поднимает `ok` → `warning` | Правило вердикта — в `libs/verify` по каталогу `docs/rules.md` (CLAUDE.md «Куда добавлять»); все пути постановки с ИНН сходятся в `add_for_user` |
| 5 | Несовпадение — не «проблема»: поставщик может законно перепродавать товар заявителя, поэтому рекомендация — запросить подтверждение цепочки поставки | Не юридическое заключение (Won't, АРХ §2) |
| 6 | F9: `POST /portfolio/import` (`text/csv`, ≤ 1 МБ и ≤ 1000 строк, иначе 413) → `ImportReport` (АРХ §8); C6 `import_portfolio`; разбор CSV — чистая функция с fuzz-целью (АРХ §10) | Контракт C7 уже описан в АРХ; разбор CSV — входные данные пользователя |
| 7 | CSV: колонки «SKU; номер; ИНН поставщика», разделитель `;` или `,`, кавычки по RFC 4180, BOM и заголовок пропускаются; строки добавляются тем же путём, что и одиночная постановка; не найденные в данных — тоже на контроль (как одиночная постановка) и в отчёт | Excel в русской локали сохраняет CSV через `;`; одно поведение для одного и многих документов |
| 8 | Импорт не расходует лимит 30 проверок в минуту построчно: одна операция импорта = одно обращение к лимиту | Иначе 500 строк упирались бы в лимит АРХ §10 |

### 8.3. Работы

| # | Модуль | Результат | Тест |
|---|---|---|---|
| 4.1 | `libs/contracts`, `libs/snapshot`, `libs/verify` | `Snapshot::by_registry_id`, `parse_registry_url`, поиск по ссылке | `SnapshotTest.*`, `VerifyRegistryUrl.*`, fuzz |
| 4.2 | `apps/certd`, `bot` | Ссылка реестра в `check_text` → карточка; сообщение «записи по ссылке нет в данных» | `DomainPgTest.*`, `BotTest.*` |
| 4.3 | `web` | `bridge.canScanQr`, `scanQr`; кнопка на «Добавить» | `bridge.test.ts`, `Add.test.tsx` |
| 4.4 | `libs/verify`, `docs/rules.md`, `certd` | F8: правила сверки ИНН, предупреждение в боте и web | `RulesTest.Table`, `DomainPgTest.*`, `Card.*` |
| 4.5 | `apps/certd`, `openapi.yaml`, `web` | F9: разбор CSV, `import_portfolio`, `POST /portfolio/import`, экран «Импорт» | `CsvImport.*`, `DomainPgTest.Import500RowsUnder10s`, `RestApiTest.*`, `Import.test.tsx`, fuzz `fuzz_import_csv`, schemathesis |

### 8.4. Результат ворот (F10, F8, F9; чистый клон, `scripts/gate.sh`)

| # | Пункт | Результат |
|---|---|---|
| 1 | GCC 13 и clang 18 с `-Werror`, clang-tidy, clang-format, ESLint, tsc strict, линтер OpenAPI | 0 ошибок, 0 предупреждений |
| 2 | Тесты ×2, ASan/UBSan, TSan | C++ 245/245 (дважды, с PostgreSQL, 0 пропущенных); web 75/75 (дважды); санитайзеры без находок |
| 3 | Покрытие | C++ 89,7% строк (3626/4042); web 99,4% строк |
| 4 | Шаги CI локально | все зелёные; fuzz — 9 целей × 60 с (новые `fuzz_registry_url`, `fuzz_import_csv`) без находок |
| 5 | `docker build --no-cache` / compose | 75 с (без Tesseract, ADR-0017); compose-smoke и контрактные тесты зелёные |
| 6 | Критерии F | таблица 8.5 |
| 7 | Соответствие АРХ, циклы | `deps-check.sh` OK; отклонений от АРХ нет, решения — §8.2 |
| 8 | Документация | README модулей и корневой, CLAUDE.md, `docs/rules.md`, `openapi.yaml` 1.5.0, журнал контрактов (C2, C4, C6, C7) |
| 9 | gitleaks, авторство | утечек нет; соавторства и упоминаний ИИ нет |

Бенчмарки: точный поиск p99 3,8 мкс, нечёткий p99 244 мкс, diff 68 мс на 1 млн записей.

Найдено воротами: schemathesis — лимит 30 проверок в минуту общий для проверок, файлов и импорта, поэтому прогон
одним пользователем получает 429 (задокументированный ответ добавлен в ожидаемые); тело `text/csv` описано без
JSON-типа — «число 0» и «строка "0"» для текста неотличимы. На живом стеке `Allow` для `/portfolio/import` включал
`DELETE` от шаблона `/portfolio/{id}` — статичный путь теперь важнее шаблона.

### 8.5. F-требования этапа 4 → код → тест

| F | Критерий приёмки (АРХ §2) | Код | Тест |
|---|---|---|---|
| F10 | Сканирование QR камерой в мини-приложении работает на телефоне; на вебе кнопка скрыта, работает загрузка файла | `web/src/max/bridge.ts` (`canScanQr`, `scanQr`), `web/src/screens/Add.tsx`; `verify::parse_registry_url`, C2 `Snapshot::by_registry_id`, `apps/certd/registry_link.cpp` | web `bridge.test.ts` «сканер QR», `Add.test.tsx` «на телефоне…», «в вебе кнопки сканера нет»; `SnapshotTest.ByRegistryId`, `VerifyText.ParseRegistryUrl`, `DomainPgTest.RegistryLinkFromQr`, `BotTest.RegistryLinkGivesCard`, fuzz `fuzz_registry_url` |
| F8 | Несовпадение «заявитель = поставщик» → предупреждение с объяснением | `verify.cpp` (`supplier_rules`, `advice.check_supplier`), `Query::supplier_inn`, `bot/card.cpp` (`supplier_attached`), `web/src/screens/Add.tsx` | `RulesTest.SupplierRules`, `DomainPgTest.SupplierMismatchOnAdd`, `BotTest.SupplierMismatchWarns`, web `Add.test.tsx` «F8…» |
| F9 | Импорт CSV «SKU, номер, ИНН»: 500 строк ≤ 10 с, отчёт по ненайденным | `apps/certd/csv_import.cpp`, `import_report.cpp`, `DomainServiceImpl::import_portfolio`, `POST /portfolio/import`, `web/src/screens/Import.tsx` | `DomainPgTest.Import500RowsUnder10s` (≈ 2 с), `CsvImport.*`, `RestApiTest.ImportCsv`, web `Import.test.tsx`, fuzz `fuzz_import_csv`, schemathesis |

