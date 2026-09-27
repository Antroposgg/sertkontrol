import { Button } from '@maxhub/max-ui';
import { useCallback, useState } from 'react';

import { useApi } from '../api/context';
import { problemMessage, type Problem } from '../api/problem';
import type { DataStatus } from '../api/types';
import { StateView } from '../components/StateView';
import { formatDate, simulateResultText } from '../format';
import { toProblemOf, useResource } from '../hooks/useResource';

/** Итог последнего демо-действия. */
type DemoAction = { kind: 'idle' } | { kind: 'busy' } | { kind: 'done'; text: string } | { kind: 'error'; problem: Problem };

/** Свойства блока демо-сценария. */
interface DemoControlsProps {
  status: DataStatus;
  action: DemoAction;
  onSimulate: () => void;
  onReset: () => void;
}

/** Блок демо-сценария F6: стадия N / N+1, «Симулировать обновление», «Сбросить демо». */
function DemoControls({ status, action, onSimulate, onReset }: DemoControlsProps) {
  const updated = status.demo_stage === 'updated';
  const busy = action.kind === 'busy';
  return (
    <section aria-label="Демо-сценарий" className="demo-controls">
      <h3>Демо-сценарий</h3>
      <p>
        {updated
          ? 'Показаны данные после обновления (снапшот N+1).'
          : 'Показаны исходные данные (снапшот N). Поставьте документ на контроль и нажмите «Симулировать обновление» — бот пришлёт уведомление о смене статуса.'}
      </p>
      <div className="actions">
        {updated ? (
          <Button variant="secondary" onClick={onReset} loading={busy} disabled={busy}>
            Сбросить демо
          </Button>
        ) : (
          <Button onClick={onSimulate} loading={busy} disabled={busy || !status.demo_update_available}>
            Симулировать обновление
          </Button>
        )}
      </div>
      {!updated && !status.demo_update_available && (
        <p>Обновлённый демо-снапшот ещё загружается — повторите через минуту.</p>
      )}
    </section>
  );
}

/** Экран «Данные»: версия и дата данных реестра, для демо-пользователя — демо-сценарий (F6). */
export function Data() {
  const api = useApi();
  const load = useCallback(() => api.dataStatus(), [api]);
  const { state, reload } = useResource(load);
  // Итог действия живёт здесь, а не в блоке: после действия экран перезагружает статус, и блок пересоздаётся.
  const [action, setAction] = useState<DemoAction>({ kind: 'idle' });

  const run = (task: () => Promise<string>) => {
    setAction({ kind: 'busy' });
    task().then(
      (text) => {
        setAction({ kind: 'done', text });
        reload();
      },
      (e: unknown) => {
        setAction({ kind: 'error', problem: toProblemOf(e) });
      },
    );
  };

  return (
    <>
      <StateView state={state} emptyText="" onRetry={reload}>
        {(d) => (
          <>
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
            {d.is_demo && (
              <DemoControls
                status={d}
                action={action}
                onSimulate={() => {
                  run(() => api.simulateUpdate().then((r) => simulateResultText(r.notified)));
                }}
                onReset={() => {
                  run(() => api.resetDemo().then(() => 'Демо сброшено к исходным данным — сценарий можно пройти заново.'));
                }}
              />
            )}
          </>
        )}
      </StateView>
      {action.kind === 'done' && <p role="status">{action.text}</p>}
      {action.kind === 'error' && <p role="alert">{problemMessage(action.problem)}</p>}
    </>
  );
}
