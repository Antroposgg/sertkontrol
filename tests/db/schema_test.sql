-- Проверки инвариантов схемы (АРХ §6, §7.5). Выполняется после миграций; любая ошибка — провал.
\set ON_ERROR_STOP 1
BEGIN;

INSERT INTO app_user (max_user_id) VALUES (1001), (1002);
INSERT INTO portfolio_item (user_id, doc_key, doc_kind)
  SELECT id, 'RUD-CR.PA08.B.89369/26', 'declaration' FROM app_user WHERE max_user_id = 1001;

-- NULLS NOT DISTINCT: второй раз тот же документ без SKU — дубликат.
DO $$
BEGIN
  INSERT INTO portfolio_item (user_id, doc_key, doc_kind)
    SELECT id, 'RUD-CR.PA08.B.89369/26', 'declaration' FROM app_user WHERE max_user_id = 1001;
  RAISE EXCEPTION 'FAIL: дубликат с sku IS NULL принят';
EXCEPTION WHEN unique_violation THEN
  RAISE NOTICE 'ok: portfolio_item NULLS NOT DISTINCT';
END $$;

-- Идемпотентный fan-out уведомлений.
INSERT INTO snapshot_version (version, source, source_date, file_path, status, is_demo)
  VALUES (1, 'demo', '2026-09-26', '/data/snapshots/snap-1.bin', 'ready', true);
INSERT INTO notification (portfolio_item_id, version, kind) SELECT id, 1, 'status_changed' FROM portfolio_item;
DO $$
DECLARE n integer;
BEGIN
  INSERT INTO notification (portfolio_item_id, version, kind)
    SELECT id, 1, 'status_changed' FROM portfolio_item ON CONFLICT DO NOTHING;
  GET DIAGNOSTICS n = ROW_COUNT;
  IF n <> 0 THEN RAISE EXCEPTION 'FAIL: повторный fan-out вставил % строк', n; END IF;
  RAISE NOTICE 'ok: notification идемпотентна';
END $$;

-- Дедупликация входящих событий.
INSERT INTO inbound_update (dedup_key) VALUES ('m:abc');
DO $$
DECLARE n integer;
BEGIN
  INSERT INTO inbound_update (dedup_key) VALUES ('m:abc') ON CONFLICT DO NOTHING;
  GET DIAGNOSTICS n = ROW_COUNT;
  IF n <> 0 THEN RAISE EXCEPTION 'FAIL: повтор webhook не отброшен'; END IF;
  RAISE NOTICE 'ok: inbound_update дедуплицирует';
END $$;

-- CHECK-ограничения.
DO $$
BEGIN
  INSERT INTO snapshot_version (version, source, source_date, file_path, status) VALUES (2, 'x', now(), 'p', 'bogus');
  RAISE EXCEPTION 'FAIL: недопустимый status принят';
EXCEPTION WHEN check_violation THEN
  RAISE NOTICE 'ok: snapshot_version.status CHECK';
END $$;
DO $$
BEGIN
  INSERT INTO supplier (user_id, inn) SELECT id, '12345' FROM app_user LIMIT 1;
  RAISE EXCEPTION 'FAIL: ИНН неверной длины принят';
EXCEPTION WHEN check_violation THEN
  RAISE NOTICE 'ok: supplier.inn CHECK';
END $$;
DO $$
BEGIN
  INSERT INTO snapshot_version (version, source, source_date, file_path, status, is_demo, demo_stage)
    VALUES (3, 'fsa', now(), 'p', 'ready', false, 1);
  RAISE EXCEPTION 'FAIL: demo_stage у боевой версии принят';
EXCEPTION WHEN check_violation THEN
  RAISE NOTICE 'ok: snapshot_version.demo_stage только у демо';
END $$;

ROLLBACK;
