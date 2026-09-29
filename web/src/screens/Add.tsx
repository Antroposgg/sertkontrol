import { Button, Input } from '@maxhub/max-ui';
import { useState } from 'react';

import { useApi } from '../api/context';
import { problemMessage, type Problem } from '../api/problem';
import type { CheckedVerdict } from '../api/types';
import { StateView, type ViewState } from '../components/StateView';
import { VerdictCard } from '../components/VerdictCard';
import { toProblemOf } from '../hooks/useResource';

type Result = ViewState<CheckedVerdict[]> | { kind: 'idle' };

/**
 * Экран «Добавить»: проверка номера или PDF-выписки (F1–F3) и постановка на контроль (F4).
 * Загрузка файла работает везде; сканер QR камерой (`openCodeReader`, F10) — этап 4.
 */
export function Add() {
  const api = useApi();
  const [number, setNumber] = useState('');
  const [sku, setSku] = useState('');
  const [result, setResult] = useState<Result>({ kind: 'idle' });
  const [lastRun, setLastRun] = useState<(() => void) | null>(null);
  const [watched, setWatched] = useState<Record<string, 'saving' | 'done' | Problem>>({});

  const run = (task: () => Promise<CheckedVerdict[]>) => {
    const go = () => {
      setResult({ kind: 'loading' });
      task().then(
        (verdicts) => {
          setResult(verdicts.length === 0 ? { kind: 'empty' } : { kind: 'ready', data: verdicts });
        },
        (e: unknown) => {
          setResult({ kind: 'error', problem: toProblemOf(e) });
        },
      );
    };
    setLastRun(() => go);
    go();
  };

  const watch = (v: CheckedVerdict) => {
    const key = v.number ?? v.query;
    setWatched((w) => ({ ...w, [key]: 'saving' }));
    api
      .addToPortfolio({ number: v.number ?? v.query, ...(sku.trim() === '' ? {} : { sku: sku.trim() }) })
      .then(
        () => {
          setWatched((w) => ({ ...w, [key]: 'done' }));
        },
        (e: unknown) => {
          setWatched((w) => ({ ...w, [key]: toProblemOf(e) }));
        },
      );
  };

  return (
    <div>
      <form
        onSubmit={(e) => {
          e.preventDefault();
          const n = number.trim();
          if (n !== '') run(() => api.check(n).then((v) => [v]));
        }}
      >
        <Input
          aria-label="Номер документа"
          placeholder="ЕАЭС N RU Д-RU.РА01.В.12345/23"
          value={number}
          onChange={(e) => {
            setNumber(e.target.value);
          }}
        />
        <Input
          aria-label="SKU (необязательно)"
          placeholder="SKU (необязательно)"
          value={sku}
          onChange={(e) => {
            setSku(e.target.value);
          }}
        />
        <Button type="submit" disabled={number.trim() === ''}>
          Проверить
        </Button>
      </form>
      <label className="file">
        Или PDF-выписка из реестра:{' '}
        <input
          type="file"
          accept="application/pdf,.pdf"
          aria-label="PDF-выписка"
          onChange={(e) => {
            const file = e.target.files?.[0];
            if (file !== undefined) run(() => api.checkFile(file));
            e.target.value = '';
          }}
        />
      </label>
      {result.kind !== 'idle' && (
        <StateView state={result} emptyText="В файле не найдено номеров." onRetry={() => lastRun?.()}>
          {(verdicts) => (
            <>
              {verdicts.map((v) => {
                const status = watched[v.number ?? v.query];
                return (
                  <div key={v.check_id}>
                    <VerdictCard
                      verdict={v}
                      {...(status === 'done' ? {} : { onWatch: () => { watch(v); } })}
                      onConfirm={(n) => {
                        setNumber(n);
                        run(() => api.check(n).then((checked) => [checked]));
                      }}
                      watching={status === 'saving'}
                    />
                    {status === 'done' && <p role="status">На контроле — пришлём уведомление в чат, если статус изменится.</p>}
                    {typeof status === 'object' && <p role="alert">{problemMessage(status)}</p>}
                  </div>
                );
              })}
            </>
          )}
        </StateView>
      )}
    </div>
  );
}
