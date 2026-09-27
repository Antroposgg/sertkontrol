# План реализации Сертконтроля

Источники истины:

- [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) (далее — АРХ) — стек, структура, контракты C1–C9, алгоритмы, CI;
- [`docs/CASE_REQUIREMENTS.md`](CASE_REQUIREMENTS.md) (далее — КЕЙС) — ограничения и формат сдачи. При конфликте КЕЙС важнее.

Работа идёт этапами 0–4. Этап начинается только по команде, заканчивается ворота-проверкой (см. [`CLAUDE.md`](../CLAUDE.md#ворота-проверка)).

**Текущий этап: 1 — завершён 2026-09-27 (ворота зелёные). Следующий — 2, по команде.**

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
| `ingest --demo / --once / --daemon` пишут «не реализовано» и выходят с кодом 0 | `apps/ingest/main.cpp` | Реальные режимы | `--demo` ✔ 1; `--daemon` — 2 |
| `/healthz` не требует снапшот | `apps/certd/health.cpp` (`snapshot_required=false`) | Требовать загруженный снапшот | 1 ✔ |

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
| 1 | Проверка документа на демо-снапшоте (F1 точный, F2, F3, F4) | ✔ завершён 2026-09-27, ворота зелёные |
| 2 | Обновление и уведомления (F5, F6) | ожидает команды |
| 3 | Нечёткий поиск и надёжность | ожидает команды |
| 4 | Should: F7–F10 | по отдельной команде |

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
| Кнопки | `inline_keyboard`: ≤ 30 рядов, ≤ 7 кнопок в ряду, ≤ 3 для `link`/`open_app`; `callback.payload` ≤ **1024** символа; `open_app` требует `web_app` (публичное имя бота), `payload` `^[\w-]{0,512}$` | Payload `<действие>:<id>`; `MAX_BOT_USERNAME` для `open_app` |
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
| Отправитель outbox без token bucket и джиттера | `apps/certd/outbox_sender` | Двухуровневый лимитер (АРХ §7.6) | 2 |
| Загрузка снапшота при старте + опрос раз в 5 с | `apps/certd/snapshot_loader` | `LISTEN snapshot_ready` и swap | 2 |
| `simulate_update` / `reset_demo` возвращают `forbidden` | `DomainServiceImpl` | Демо-режим N/N+1 | 2 |
| Фото → 415 | `libs/recog` | OCR (F7), QR с камеры (F10) | 4 |
| Кнопка «Указать поставщика» в боте отсутствует (ИНН поставщика — в мини-приложении) | `apps/certd/bot` | Диалог ввода ИНН | 2 |
| Экран «Документ» отсутствует; «Данные» — без демо-кнопки | `web/` | Карточка с историей, «Симулировать обновление» | 2 |
| Пагинация портфеля в интерфейсе («Показать ещё») | `web/src/screens/Portfolio.tsx` | Кнопка по `next_cursor` | 2 |

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
