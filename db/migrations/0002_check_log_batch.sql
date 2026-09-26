-- 0002_check_log_batch.sql — пачка проверок из одного сообщения или файла.
-- Кнопка бота «Поставить все на контроль» несёт id пачки (`W:<batch_id>`), а не список номеров (АРХ §8).
ALTER TABLE check_log ADD COLUMN batch_id bigint;
CREATE INDEX check_log_batch_idx ON check_log (batch_id) WHERE batch_id IS NOT NULL;
