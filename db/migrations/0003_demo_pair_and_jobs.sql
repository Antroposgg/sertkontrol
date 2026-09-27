-- 0003_demo_pair_and_jobs.sql — этап 2: демо-пара N/N+1 (C8), ошибки задач, индексы истории.

-- Роль демо-версии: 0 — снапшот N, 1 — снапшот N+1 (АРХ §4, «Демо-вариант потока B»).
-- У боевых версий NULL. certd держит открытыми последние ready-версии каждой роли.
ALTER TABLE snapshot_version
    ADD COLUMN demo_stage smallint CHECK (demo_stage IN (0, 1)),
    ADD CONSTRAINT snapshot_version_demo_stage_only_demo CHECK (demo_stage IS NULL OR is_demo);
-- Этап 1 регистрировал только демо-снапшот N.
UPDATE snapshot_version SET demo_stage = 0 WHERE is_demo AND demo_stage IS NULL;

-- Задача после исчерпания попыток остаётся в таблице с причиной (run_at = 'infinity').
ALTER TABLE job ADD COLUMN last_error text;

-- История документа (GET /api/v1/history) и выборка строк портфеля пачками по версии в notify_changes.
CREATE INDEX registry_change_doc_key_idx ON registry_change (doc_key);
CREATE INDEX portfolio_item_last_version_idx ON portfolio_item (last_version);
