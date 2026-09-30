import { Button, Input } from '@maxhub/max-ui';
import { useState } from 'react';

import { useApi } from '../api/context';
import { problemMessage, type Problem } from '../api/problem';
import type { AddResult, CheckedVerdict } from '../api/types';
import { StateView, type ViewState } from '../components/StateView';
import { VerdictCard } from '../components/VerdictCard';
import { toProblemOf } from '../hooks/useResource';
import { canScanQr, scanQr } from '../max/bridge';

type Result = ViewState<CheckedVerdict[]> | { kind: 'idle' };

/** Поставлено на контроль; `notes` — итог сверки «заявитель = поставщик» (F8), если указан ИНН. */
interface Watched {
  done: true;
  notes: string[];
}

/** Строки сверки с поставщиком из вердикта постановки (правила `supplier.*`, `advice.check_supplier`, docs/rules.md). */
function supplierNotes(result: AddResult): string[] {
  return result.verdict.findings
    .filter((f) => f.rule.startsWith('supplier.') || f.rule === 'advice.check_supplier')
    .map((f) => f.text);
}

/**
 * Экран «Добавить»: проверка номера, PDF-выписки или QR (F1–F3, F10) и постановка на контроль (F4).
 * Загрузка файла работает везде; сканер QR (`openCodeReader`) — только в мобильных клиентах MAX. Текст QR — обычно
 * ссылка на запись реестра — уходит в ту же проверку `GET /check`: сервер находит документ по ID записи.
 */
export function Add() {
  const api = useApi();
  const [number, setNumber] = useState('');
  const [sku, setSku] = useState('');
  const [supplierInn, setSupplierInn] = useState('');
  const [result, setResult] = useState<Result>({ kind: 'idle' });
  const [lastRun, setLastRun] = useState<(() => void) | null>(null);
  const [scanNote, setScanNote] = useState<string | null>(null);
  const [watched, setWatched] = useState<Record<string, 'saving' | Watched | Problem>>({});

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
      .addToPortfolio({
        number: v.number ?? v.query,
        ...(sku.trim() === '' ? {} : { sku: sku.trim() }),
        ...(supplierInn.trim() === '' ? {} : { supplier_inn: supplierInn.trim() }),
      })
      .then(
        (added) => {
          setWatched((w) => ({ ...w, [key]: { done: true, notes: supplierNotes(added) } }));
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
        <Input
          aria-label="ИНН поставщика (необязательно)"
          placeholder="ИНН поставщика (необязательно) — сверим с заявителем"
          inputMode="numeric"
          value={supplierInn}
          onChange={(e) => {
            setSupplierInn(e.target.value);
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
      {canScanQr() && (
        <Button
          variant="secondary"
          onClick={() => {
            setScanNote(null);
            void scanQr().then((scan) => {
              if (scan.kind === 'text') {
                run(() => api.check(scan.text).then((v) => [v]));
              } else if (scan.kind === 'cancelled') {
                setScanNote(`Сканер закрыт без результата${scan.code === undefined ? '' : ` (${scan.code})`}.`);
              } else {
                setScanNote(`Не получилось прочитать QR: ${scan.detail}.`);
              }
            });
          }}
        >
          Сканировать QR с выписки
        </Button>
      )}
      {scanNote !== null && <p role="status">{scanNote}</p>}
      {result.kind !== 'idle' && (
        <StateView state={result} emptyText="В файле не найдено номеров." onRetry={() => lastRun?.()}>
          {(verdicts) => (
            <>
              {verdicts.map((v) => {
                const status = watched[v.number ?? v.query];
                const done = typeof status === 'object' && 'done' in status ? status : undefined;
                return (
                  <div key={v.check_id}>
                    <VerdictCard
                      verdict={v}
                      {...(done !== undefined ? {} : { onWatch: () => { watch(v); } })}
                      onConfirm={(n) => {
                        setNumber(n);
                        run(() => api.check(n).then((checked) => [checked]));
                      }}
                      watching={status === 'saving'}
                    />
                    {done !== undefined && (
                      <>
                        <p role="status">На контроле — пришлём уведомление в чат, если статус изменится.</p>
                        {done.notes.length > 0 && (
                          <ul aria-label="Сверка с поставщиком">
                            {done.notes.map((n) => (
                              <li key={n}>{n}</li>
                            ))}
                          </ul>
                        )}
                      </>
                    )}
                    {typeof status === 'object' && !('done' in status) && <p role="alert">{problemMessage(status)}</p>}
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
