import { Button, CellList, CellSimple, Input } from '@maxhub/max-ui';
import { useCallback, useState } from 'react';

import { useApi } from '../api/context';
import type { PortfolioItem, Status } from '../api/types';
import { StateView } from '../components/StateView';
import { STATUS_NAMES } from '../format';
import { toProblemOf, useResource } from '../hooks/useResource';
import { problemMessage } from '../api/problem';

const FILTERS: (Status | 'all')[] = ['all', 'active', 'suspended', 'terminated', 'annulled', 'archived'];

/** Экран «Портфель» (F4): список документов на контроле, фильтры по статусу и ИНН поставщика, снятие с контроля. */
export function Portfolio() {
  const api = useApi();
  const [status, setStatus] = useState<Status | 'all'>('all');
  const [inn, setInn] = useState('');
  const [appliedInn, setAppliedInn] = useState('');
  const [actionError, setActionError] = useState<string | null>(null);

  const load = useCallback(
    () =>
      api.listPortfolio({
        ...(status === 'all' ? {} : { status }),
        ...(appliedInn === '' ? {} : { supplierInn: appliedInn }),
      }),
    [api, status, appliedInn],
  );
  const { state, reload } = useResource(load, (page) => page.items.length === 0);

  const remove = (item: PortfolioItem) => {
    setActionError(null);
    api.removeFromPortfolio(item.id).then(reload, (e: unknown) => {
      setActionError(problemMessage(toProblemOf(e)));
    });
  };

  return (
    <div>
      <label>
        Статус{' '}
        <select
          value={status}
          onChange={(e) => {
            setStatus(e.target.value as Status | 'all');
          }}
        >
          {FILTERS.map((f) => (
            <option key={f} value={f}>
              {f === 'all' ? 'все' : STATUS_NAMES[f]}
            </option>
          ))}
        </select>
      </label>
      <form
        onSubmit={(e) => {
          e.preventDefault();
          setAppliedInn(inn.trim());
        }}
      >
        <Input
          aria-label="ИНН поставщика"
          placeholder="ИНН поставщика"
          inputMode="numeric"
          value={inn}
          onChange={(e) => {
            setInn(e.target.value);
          }}
        />
        <Button type="submit" variant="secondary" size="small">
          Найти
        </Button>
      </form>
      {actionError !== null && <p role="alert">{actionError}</p>}
      <StateView
        state={state}
        emptyText="Документов на контроле нет. Добавьте номер на вкладке «Добавить» или нажмите «На контроль» в боте."
        onRetry={reload}
      >
        {(page) => (
          <CellList>
            {page.items.map((item) => (
              <CellSimple
                key={item.id}
                title={item.display_number}
                subtitle={[STATUS_NAMES[item.last_status], item.sku, item.supplier_inn && `ИНН ${item.supplier_inn}`]
                  .filter(Boolean)
                  .join(' · ')}
                after={
                  <Button
                    variant="ghost"
                    size="small"
                    aria-label={`Снять с контроля ${item.display_number}`}
                    onClick={() => {
                      remove(item);
                    }}
                  >
                    Снять
                  </Button>
                }
              />
            ))}
          </CellList>
        )}
      </StateView>
    </div>
  );
}
