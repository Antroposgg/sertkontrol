import { Button, Typography } from '@maxhub/max-ui';
import { useCallback, useState, type ReactNode } from 'react';

import { useApi } from '../api/context';
import { problemMessage } from '../api/problem';
import { toProblemOf, useResource } from '../hooks/useResource';
import { StateView } from './StateView';

/**
 * Показывает приложение только после согласия на обработку данных (АРХ §10) — тот же текст, что в боте.
 * До согласия REST отвечает `403 consent_required`, поэтому экраны без него бесполезны.
 */
export function ConsentGate({ children }: { children: ReactNode }) {
  const api = useApi();
  const load = useCallback(() => api.me(), [api]);
  const { state, reload } = useResource(load);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  return (
    <StateView state={state} emptyText="" onRetry={reload}>
      {(me) =>
        me.consented ? (
          <>{children}</>
        ) : (
          <section aria-label="Согласие">
            <Typography.Title>Согласие на обработку данных</Typography.Title>
            <Typography.Body>
              Сертконтроль хранит ваш ID в MAX, документы на контроле и ИНН поставщиков. Присланные файлы не сохраняются.
            </Typography.Body>
            {error !== null && <p role="alert">{error}</p>}
            <Button
              loading={saving}
              disabled={saving}
              onClick={() => {
                setSaving(true);
                setError(null);
                api.consent().then(
                  () => {
                    setSaving(false);
                    reload();
                  },
                  (e: unknown) => {
                    setSaving(false);
                    setError(problemMessage(toProblemOf(e)));
                  },
                );
              }}
            >
              Согласен
            </Button>
          </section>
        )
      }
    </StateView>
  );
}
