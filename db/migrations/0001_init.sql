-- 0001_init.sql — начальная схема (АРХ §6 «PostgreSQL»). Контракты C8 и C9.
-- Реестр в PostgreSQL не хранится (ADR-0001): здесь только изменяемые данные.
-- Миграция применяется один раз мигратором db/migrate.sh в одной транзакции.

-- ─────────────── Пользователи и портфель (R3) ───────────────

CREATE TABLE app_user (
    id           bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    max_user_id  bigint      NOT NULL UNIQUE,
    consent_at   timestamptz,
    -- Пока данные ФСА не подтверждены, все пользователи работают в демо-режиме (КЕЙС §2 п.10).
    is_demo      boolean     NOT NULL DEFAULT true,
    -- 0 — пользователь видит снапшот N, 1 — N+1 (АРХ §4 «Демо-вариант потока B»).
    demo_stage   smallint    NOT NULL DEFAULT 0 CHECK (demo_stage IN (0, 1)),
    created_at   timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE supplier (
    id       bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    user_id  bigint NOT NULL REFERENCES app_user (id) ON DELETE CASCADE,
    -- Контрольные цифры ИНН проверяются в коде (АРХ §7.7); здесь — только формат.
    inn      text   NOT NULL CHECK (inn ~ '^([0-9]{10}|[0-9]{12})$'),
    name     text,
    UNIQUE (user_id, inn)
);

CREATE TABLE portfolio_item (
    id            bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    user_id       bigint      NOT NULL REFERENCES app_user (id) ON DELETE CASCADE,
    doc_key       text        NOT NULL,  -- каноническая форма номера (C1)
    doc_kind      text        NOT NULL CHECK (doc_kind IN ('declaration', 'certificate')),
    sku           text,
    supplier_id   bigint      REFERENCES supplier (id) ON DELETE SET NULL,
    last_status   text        NOT NULL DEFAULT 'unknown'
        CHECK (last_status IN ('unknown', 'active', 'suspended', 'terminated', 'annulled', 'archived')),
    last_version  bigint,               -- версия снапшота, на которой зафиксирован last_status
    created_at    timestamptz NOT NULL DEFAULT now(),
    -- NULLS NOT DISTINCT (PG 15+): две строки с sku IS NULL — дубликат.
    CONSTRAINT portfolio_item_uniq UNIQUE NULLS NOT DISTINCT (user_id, doc_key, sku)
);
CREATE INDEX portfolio_item_doc_key_idx ON portfolio_item (doc_key);

-- ─────────────── Версии данных и изменения (C8, пишет R1) ───────────────

CREATE TABLE snapshot_version (
    version       bigint      PRIMARY KEY,
    source        text        NOT NULL,
    source_date   date        NOT NULL,
    file_path     text        NOT NULL,
    record_count  bigint      NOT NULL DEFAULT 0 CHECK (record_count >= 0),
    status        text        NOT NULL CHECK (status IN ('building', 'ready', 'failed')),
    stats         jsonb       NOT NULL DEFAULT '{}'::jsonb,
    is_demo       boolean     NOT NULL DEFAULT false,
    created_at    timestamptz NOT NULL DEFAULT now()
);
-- После перевода версии в 'ready' ingest выполняет: NOTIFY snapshot_ready, '<version>' (C8).
-- certd слушает канал snapshot_ready (LISTEN) и открывает файл file_path.

CREATE TABLE registry_change (
    version      bigint NOT NULL REFERENCES snapshot_version (version) ON DELETE CASCADE,
    doc_key      text   NOT NULL,
    -- {"status": "...", "expiry_date": "YYYY-MM-DD", "status_date": "YYYY-MM-DD"}; NULL — записи не было / не стало.
    before       jsonb,
    after        jsonb,
    status_date  date,
    PRIMARY KEY (version, doc_key)
);

-- ─────────────── Уведомления и очереди (R3 пишет, R4 отправляет) ───────────────

CREATE TABLE notification (
    id                 bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    portfolio_item_id  bigint      NOT NULL REFERENCES portfolio_item (id) ON DELETE CASCADE,
    version            bigint      NOT NULL REFERENCES snapshot_version (version) ON DELETE CASCADE,
    kind               text        NOT NULL,
    created_at         timestamptz NOT NULL DEFAULT now(),
    -- Идемпотентный fan-out: повторный notify_changes(v) не дублирует уведомления (АРХ §7.5).
    UNIQUE (portfolio_item_id, version, kind)
);

CREATE TABLE inbound_update (
    dedup_key     text        PRIMARY KEY,  -- m:<mid> или c:<callback_id> (АРХ §4, поток A)
    received_at   timestamptz NOT NULL DEFAULT now(),
    processed_at  timestamptz,
    error         text
);

-- C9: payload — JSON OutgoingMessage, схема в docs/contracts/outgoing_message.schema.json.
CREATE TABLE outbox (
    id          bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    user_id     bigint      NOT NULL REFERENCES app_user (id) ON DELETE CASCADE,
    payload     jsonb       NOT NULL,
    priority    smallint    NOT NULL DEFAULT 0,  -- больше — раньше («Проверяю…» важнее рассылки)
    not_before  timestamptz NOT NULL DEFAULT now(),
    attempts    integer     NOT NULL DEFAULT 0 CHECK (attempts >= 0),
    status      text        NOT NULL DEFAULT 'pending'
        CHECK (status IN ('pending', 'sending', 'sent', 'failed')),
    last_error  text,
    created_at  timestamptz NOT NULL DEFAULT now(),
    sent_at     timestamptz
);
CREATE INDEX outbox_pick_idx ON outbox (status, priority, not_before);

CREATE TABLE job (
    id            bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    kind          text        NOT NULL,
    dedup_key     text,
    run_at        timestamptz NOT NULL DEFAULT now(),
    payload       jsonb       NOT NULL DEFAULT '{}'::jsonb,
    locked_until  timestamptz,
    attempts      integer     NOT NULL DEFAULT 0 CHECK (attempts >= 0),
    created_at    timestamptz NOT NULL DEFAULT now()
);
-- Выборка: … WHERE run_at <= now() AND (locked_until IS NULL OR locked_until < now()) FOR UPDATE SKIP LOCKED.
CREATE INDEX job_run_at_idx ON job (run_at);
CREATE UNIQUE INDEX job_dedup_idx ON job (kind, dedup_key) WHERE dedup_key IS NOT NULL;

-- ─────────────── Диалоги и журнал (R4, R3) ───────────────

CREATE TABLE dialog_state (
    user_id     bigint      PRIMARY KEY REFERENCES app_user (id) ON DELETE CASCADE,
    state       text        NOT NULL,
    data        jsonb       NOT NULL DEFAULT '{}'::jsonb,
    updated_at  timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE check_log (
    id          bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    user_id     bigint      REFERENCES app_user (id) ON DELETE SET NULL,
    via         text        NOT NULL CHECK (via IN ('bot_text', 'bot_file', 'app_text', 'app_file')),
    doc_key     text,
    level       text        NOT NULL,
    latency_ms  integer     NOT NULL CHECK (latency_ms >= 0),
    created_at  timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX check_log_created_at_idx ON check_log (created_at);

-- Сроки хранения (АРХ §6): check_log — 90 дней, inbound_update — 7 дней, отправленный outbox — 30 дней.
-- Очистку выполняет периодическая задача certd (этап 2).
