import { Button, CellList, CellSimple, Input } from '@maxhub/max-ui';
import { useCallback, useState } from 'react';

import { useApi } from '../api/context';
import { problemMessage } from '../api/problem';
import type { PortfolioFilter, PortfolioItem, PortfolioPage, Status } from '../api/types';
import { StateView } from '../components/StateView';
import { STATUS_NAMES } from '../format';
import { toProblemOf, useResource } from '../hooks/useResource';

const FILTERS: (Status | 'all')[] = ['all', 'active', 'suspended', 'terminated', 'annulled', 'archived'];

/**
 * Догруженные страницы. Привязаны к первой странице (`first`): новая первая страница — смена фильтра или
 * перезагрузка после снятия — отбрасывает их без эффекта и синхронного `setState`.
 */
interface More {
  first: PortfolioPage;
  items: PortfolioItem[];
  next: string | null;
  loading: boolean;
}

/** Свойства списка портфеля. */
interface ListProps {
  page: PortfolioPage;
  more: More | null;
  onMore: (cursor: string) => void;
  onRemove: (item: PortfolioItem) => void;
  onOpen?: ((number: string) => void) | undefined;
}

/** Список: первая страница, догруженные и кнопка «Показать ещё», пока есть курсор. */
function PortfolioList({ page, more, onMore, onRemove, onOpen }: ListProps) {
  const extra = more?.first === page ? more : null;
  const next = extra === null ? page.next_cursor : extra.next;
  const loading = extra?.loading === true;
  return (
    <>
      <CellList>
        {[...page.items, ...(extra?.items ?? [])].map((item) => (
          <CellSimple
            key={item.id}
            title={item.display_number}
            subtitle={[STATUS_NAMES[item.last_status], item.sku, item.supplier_inn && `ИНН ${item.supplier_inn}`]
              .filter(Boolean)
              .join(' · ')}
            after={
              <div className="actions">
                {onOpen !== undefined && (
                  <Button
                    variant="ghost"
                    size="small"
                    aria-label={`Открыть ${item.display_number}`}
                    onClick={() => {
                      onOpen(item.display_number);
                    }}
                  >
                    Открыть
                  </Button>
                )}
                <Button
                  variant="ghost"
                  size="small"
                  aria-label={`Снять с контроля ${item.display_number}`}
                  onClick={() => {
                    onRemove(item);
                  }}
                >
                  Снять
                </Button>
              </div>
            }
          />
        ))}
      </CellList>
      {next !== null && (
        <Button
          variant="secondary"
          loading={loading}
          disabled={loading}
          onClick={() => {
            onMore(next);
          }}
        >
          Показать ещё
        </Button>
      )}
    </>
  );
}

/** Свойства экрана «Портфель». */
export interface PortfolioProps {
  /** Открыть экран «Документ»; не передан — кнопки «Открыть» нет. */
  openDocument?: (number: string) => void;
}

/**
 * Экран «Портфель» (F4): список документов на контроле, фильтры по статусу и ИНН поставщика, снятие с контроля,
 * «Показать ещё» по курсору `next_cursor`, переход к экрану «Документ».
 */
export function Portfolio({ openDocument }: PortfolioProps) {
  const api = useApi();
  const [status, setStatus] = useState<Status | 'all'>('all');
  const [inn, setInn] = useState('');
  const [appliedInn, setAppliedInn] = useState('');
  const [actionError, setActionError] = useState<string | null>(null);
  const [more, setMore] = useState<More | null>(null);

  const filter = useCallback(
    (): PortfolioFilter => ({
      ...(status === 'all' ? {} : { status }),
      ...(appliedInn === '' ? {} : { supplierInn: appliedInn }),
    }),
    [status, appliedInn],
  );
  const load = useCallback(() => api.listPortfolio(filter()), [api, filter]);
  const { state, reload } = useResource(load, (page) => page.items.length === 0);

  const remove = (item: PortfolioItem) => {
    setActionError(null);
    api.removeFromPortfolio(item.id).then(reload, (e: unknown) => {
      setActionError(problemMessage(toProblemOf(e)));
    });
  };

  const showMore = (first: PortfolioPage, cursor: string) => {
    setActionError(null);
    setMore((m) => ({ first, items: m?.first === first ? m.items : [], next: cursor, loading: true }));
    api.listPortfolio({ ...filter(), cursor }).then(
      (page) => {
        setMore((m) =>
          m?.first === first ? { first, items: [...m.items, ...page.items], next: page.next_cursor, loading: false } : m,
        );
      },
      (e: unknown) => {
        setMore((m) => (m?.first === first ? { ...m, loading: false } : m));
        setActionError(problemMessage(toProblemOf(e)));
      },
    );
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
          <PortfolioList
            page={page}
            more={more}
            onMore={(cursor) => {
              showMore(page, cursor);
            }}
            onRemove={remove}
            onOpen={openDocument}
          />
        )}
      </StateView>
    </div>
  );
}
