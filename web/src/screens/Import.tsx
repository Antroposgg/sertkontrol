import { Button } from '@maxhub/max-ui';
import { useState } from 'react';

import { useApi } from '../api/context';
import type { ImportedLine, ImportReport } from '../api/types';
import { StateView, type ViewState } from '../components/StateView';
import { toProblemOf } from '../hooks/useResource';

type Result = ViewState<ImportReport> | { kind: 'idle' };

/**
 * Пример импорта на демо-данных — тот же, что `data/demo/import-example.csv`: совпадающий и чужой поставщик, номер,
 * которого нет в данных, строки с ошибками. Кнопка нужна, чтобы проверить импорт на телефоне без файла.
 */
export const IMPORT_EXAMPLE = `${[
  'SKU;Номер документа;ИНН поставщика',
  'ЧАЙНИК-01;ЕАЭС N RU Д-CR.РА08.В.89369/26;7700000016',
  'ЧАЙНИК-02;ЕАЭС N RU Д-CN.РА01.В.10001/25;7700000023',
  'ФЕН-03;ЕАЭС N RU Д-TR.РА03.В.10004/24;7700000023',
  'ЛАМПА-04;ЕАЭС RU С-CN.АЯ46.В.10006/25;',
  'УТЮГ-05;ЕАЭС N RU Д-CR.РА07.В.89369/26;',
  'ПЛИТА-06;ЕАЭС N RU Д-RU.РА04.В.10007/23;',
  'ЧАЙНИК-07;номер потерялся;',
  'ФЕН-08;ЕАЭС N RU Д-KZ.РА05.В.10009/25;1234567890',
].join('\n')}\n`;

/** Список строк отчёта «строка N — номер». */
function Lines({ title, lines }: { title: string; lines: ImportedLine[] }) {
  if (lines.length === 0) return null;
  return (
    <section aria-label={title}>
      <h3>{title}</h3>
      <ul>
        {lines.map((l) => (
          <li key={`${String(l.line)}-${l.number}`}>
            Строка {l.line}: {l.display_number}
          </li>
        ))}
      </ul>
    </section>
  );
}

/** Отчёт импорта: сколько поставлено, что не найдено, где поставщик не совпал с заявителем, какие строки отклонены. */
function Report({ report }: { report: ImportReport }) {
  return (
    <div>
      <dl>
        <dt>Строк в файле</dt>
        <dd>{report.total}</dd>
        <dt>Поставлено на контроль</dt>
        <dd>{report.added}</dd>
        <dt>Уже были на контроле</dt>
        <dd>{report.already}</dd>
      </dl>
      <Lines title="Нет в данных реестра — на контроле, проверьте номер" lines={report.not_found} />
      <Lines title="Документ оформлен не на поставщика" lines={report.supplier_mismatch} />
      {report.invalid.length > 0 && (
        <section aria-label="Не поставлены">
          <h3>Не поставлены</h3>
          <ul>
            {report.invalid.map((i) => (
              <li key={i.line}>
                Строка {i.line}: {i.reason}
              </li>
            ))}
          </ul>
        </section>
      )}
    </div>
  );
}

/**
 * Экран «Импорт» (F9): CSV «SKU; номер; ИНН поставщика» → все строки на контроль и отчёт по строкам.
 * Файл читается в браузере и уходит как `text/csv` (`POST /portfolio/import`); на сервере не сохраняется.
 */
export function Import() {
  const api = useApi();
  const [result, setResult] = useState<Result>({ kind: 'idle' });
  const [lastSource, setLastSource] = useState<(() => Promise<string>) | null>(null);

  const upload = (source: () => Promise<string>) => {
    setLastSource(() => source);
    setResult({ kind: 'loading' });
    source()
      .then((csv) => api.importPortfolio(csv))
      .then(
        (report) => {
          setResult({ kind: 'ready', data: report });
        },
        (e: unknown) => {
          setResult({ kind: 'error', problem: toProblemOf(e) });
        },
      );
  };

  return (
    <div>
      <p>
        Файл CSV — одна строка на товар: <b>SKU; номер документа; ИНН поставщика</b>. SKU и ИНН можно не заполнять, первая
        строка может быть заголовком. Подойдёт таблица, сохранённая из Excel как «CSV (разделители — точка с запятой)». До
        1000 строк.
      </p>
      <label className="file">
        CSV-файл:{' '}
        <input
          type="file"
          accept=".csv,text/csv"
          aria-label="CSV-файл"
          onChange={(e) => {
            const file = e.target.files?.[0];
            if (file !== undefined) upload(() => file.text());
            e.target.value = '';
          }}
        />
      </label>
      <Button
        variant="secondary"
        onClick={() => {
          upload(() => Promise.resolve(IMPORT_EXAMPLE));
        }}
      >
        Импортировать пример
      </Button>
      {result.kind !== 'idle' && (
        <StateView
          state={result}
          emptyText=""
          onRetry={() => {
            if (lastSource !== null) upload(lastSource);
          }}
        >
          {(report) => <Report report={report} />}
        </StateView>
      )}
    </div>
  );
}
