import { Button, Input } from '@maxhub/max-ui';
import { useCallback, useState } from 'react';

import { useApi } from '../api/context';
import { problemMessage, type Problem } from '../api/problem';
import type { CheckedVerdict, DocumentHistory } from '../api/types';
import { StateView } from '../components/StateView';
import { VerdictCard } from '../components/VerdictCard';
import { describeChange, formatDate } from '../format';
import { toProblemOf, useResource } from '../hooks/useResource';

/** Данные экрана: вердикт по текущему снапшоту и история смен статуса. */
interface DocumentData {
  verdict: CheckedVerdict;
  history: DocumentHistory;
}

/** Свойства экрана «Документ». */
export interface DocumentProps {
  /** Номер, с которым экран открыт (из портфеля); пусто — ввод вручную. */
  number?: string;
}

/**
 * Экран «Документ» (АРХ §8): карточка вердикта по текущим данным и история смены статусов из видимых
 * пользователю версий (F5). Номер — в любом написании: канонизирует сервер (ADR-0014).
 */
export function Document({ number: initial = '' }: DocumentProps) {
  const api = useApi();
  const [input, setInput] = useState(initial);
  const [number, setNumber] = useState(initial.trim());
  const [watch, setWatch] = useState<'idle' | 'saving' | 'done' | Problem>('idle');

  const load = useCallback(
    (): Promise<DocumentData | null> =>
      number === ''
        ? Promise.resolve(null)
        : Promise.all([api.check(number), api.history(number)]).then(([verdict, history]) => ({ verdict, history })),
    [api, number],
  );
  const { state, reload } = useResource(load, (d) => d === null);

  const addToPortfolio = (n: string) => {
    setWatch('saving');
    api.addToPortfolio({ number: n }).then(
      () => {
        setWatch('done');
      },
      (e: unknown) => {
        setWatch(toProblemOf(e));
      },
    );
  };

  return (
    <div>
      <form
        onSubmit={(e) => {
          e.preventDefault();
          setWatch('idle');
          setNumber(input.trim());
        }}
      >
        <Input
          aria-label="Номер документа"
          placeholder="ЕАЭС N RU Д-RU.РА01.В.12345/23"
          value={input}
          onChange={(e) => {
            setInput(e.target.value);
          }}
        />
        <Button type="submit" disabled={input.trim() === ''}>
          Показать
        </Button>
      </form>
      <StateView
        state={state}
        emptyText="Введите номер документа или откройте документ из портфеля."
        onRetry={reload}
      >
        {(d) => (d === null ? null : (
          <>
            <VerdictCard
              verdict={d.verdict}
              {...(watch === 'done' ? {} : { onWatch: () => { addToPortfolio(d.verdict.number ?? d.verdict.query); } })}
              watching={watch === 'saving'}
            />
            {watch === 'done' && <p role="status">На контроле — пришлём уведомление в чат, если статус изменится.</p>}
            {typeof watch === 'object' && <p role="alert">{problemMessage(watch)}</p>}
            <section aria-label="История статуса">
              <h3>История статуса</h3>
              {d.history.entries.length === 0 ? (
                <p>В загруженных версиях данных статус не менялся.</p>
              ) : (
                <ol className="history">
                  {d.history.entries.map((e) => (
                    <li key={e.version}>
                      <b>{formatDate(e.data_date)}</b> — {describeChange(e)}
                    </li>
                  ))}
                </ol>
              )}
            </section>
          </>
        ))}
      </StateView>
    </div>
  );
}
