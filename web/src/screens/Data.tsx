import { useCallback } from 'react';

import { useApi } from '../api/context';
import { StateView } from '../components/StateView';
import { formatDate } from '../format';
import { useResource } from '../hooks/useResource';

/** Экран «Данные»: версия и дата данных реестра. Демо-кнопка «Симулировать обновление» — этап 2. */
export function Data() {
  const api = useApi();
  const load = useCallback(() => api.dataStatus(), [api]);
  const { state, reload } = useResource(load);
  return (
    <StateView state={state} emptyText="" onRetry={reload}>
      {(d) => (
        <dl>
          <dt>Данные реестра на</dt>
          <dd>{formatDate(d.source_date)}</dd>
          <dt>Версия снапшота</dt>
          <dd>{d.version}</dd>
          <dt>Записей</dt>
          <dd>{d.record_count}</dd>
          <dt>Источник</dt>
          <dd>{d.is_demo ? 'Тестовые данные (демо-снапшот), не официальный источник' : d.source}</dd>
        </dl>
      )}
    </StateView>
  );
}
