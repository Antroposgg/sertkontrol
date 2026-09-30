import { useState } from 'react';

import { useApi } from '../api/context';
import type { ImportedLine, ImportReport } from '../api/types';
import { StateView, type ViewState } from '../components/StateView';
import { toProblemOf } from '../hooks/useResource';

type Result = ViewState<ImportReport> | { kind: 'idle' };

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
  const [lastFile, setLastFile] = useState<File | null>(null);

  const upload = (file: File) => {
    setLastFile(file);
    setResult({ kind: 'loading' });
    file
      .text()
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
            if (file !== undefined) upload(file);
            e.target.value = '';
          }}
        />
      </label>
      {result.kind !== 'idle' && (
        <StateView
          state={result}
          emptyText=""
          onRetry={() => {
            if (lastFile !== null) upload(lastFile);
          }}
        >
          {(report) => <Report report={report} />}
        </StateView>
      )}
    </div>
  );
}
