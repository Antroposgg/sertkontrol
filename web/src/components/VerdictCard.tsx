import { Button, Typography } from '@maxhub/max-ui';

import type { Basis, Verdict } from '../api/types';
import { formatDate, LEVEL_ICONS } from '../format';
import { openExternal } from '../max/bridge';

const SECTIONS: { basis: Basis; title: string }[] = [
  { basis: 'fact', title: 'Факт' },
  { basis: 'calculation', title: 'Расчёт' },
  { basis: 'recommendation', title: 'Рекомендация' },
];

/** Свойства карточки. */
export interface VerdictCardProps {
  verdict: Verdict;
  /** «На контроль»; не передан — кнопки нет. */
  onWatch?: () => void;
  /** «Да» на вопрос «Это номер …?» — проверить подсказанный номер (АРХ §4, поток A, шаг 6). */
  onConfirm?: (number: string) => void;
  watching?: boolean;
}

/**
 * Карточка вердикта (F3): статус, сроки, заявитель, изготовитель, продукция, ссылка на реестр, дата данных;
 * каждая строка — в блоке «Факт / Расчёт / Рекомендация». Содержание совпадает с карточкой бота.
 */
export function VerdictCard({ verdict, onWatch, onConfirm, watching = false }: VerdictCardProps) {
  const suggestion = verdict.suggestions[0];
  if (verdict.level === 'needs_confirmation' && suggestion !== undefined) {
    return (
      <article className="verdict" aria-label={`Вопрос о номере ${verdict.display_number ?? verdict.query}`}>
        <Typography.Title>❓ Это номер {suggestion.display_number}?</Typography.Title>
        <Typography.Body>
          Номера {verdict.display_number ?? verdict.query} нет в данных реестра на {formatDate(verdict.data_date)}. Похоже на
          ошибку распознавания или опечатку.
        </Typography.Body>
        {verdict.is_demo && <p className="demo-note">Тестовые данные: демо-снапшот реестра</p>}
        {onConfirm !== undefined && (
          <div className="actions">
            <Button
              onClick={() => {
                onConfirm(suggestion.number);
              }}
            >
              Да, проверить
            </Button>
          </div>
        )}
      </article>
    );
  }
  const card = verdict.card;
  const title = verdict.display_number ?? verdict.query;
  return (
    <article className="verdict" aria-label={`Вердикт ${title}`}>
      <Typography.Title>
        {LEVEL_ICONS[verdict.level]} {card === null ? title : `${card.kind === 'certificate' ? 'Сертификат' : 'Декларация'} ${title}`}
      </Typography.Title>
      <Typography.Body>
        {card === null ? (verdict.number === null ? 'Номер не распознан' : 'Нет в данных реестра') : card.status_name}
      </Typography.Body>
      {verdict.is_demo && <p className="demo-note">Тестовые данные: демо-снапшот реестра</p>}
      {SECTIONS.map(({ basis, title: sectionTitle }) => {
        const rows = verdict.findings.filter((f) => f.basis === basis).map((f) => f.text);
        if (basis === 'fact' && card !== null) {
          if (card.applicant_name !== '') {
            rows.push(`Заявитель: ${card.applicant_name}${card.applicant_inn === '' ? '' : `, ИНН ${card.applicant_inn}`}`);
          }
          if (card.manufacturer_name !== '') rows.push(`Изготовитель: ${card.manufacturer_name}`);
          if (card.product !== '') rows.push(`Продукция: ${card.product}${card.tnved === '' ? '' : `, ТН ВЭД ${card.tnved}`}`);
        }
        if (basis === 'fact' && verdict.suggestions.length > 0) {
          rows.push(`Похожие номера: ${verdict.suggestions.map((s) => s.display_number).join('; ')}`);
        }
        if (rows.length === 0) return null;
        return (
          <section key={basis} aria-label={sectionTitle}>
            <Typography.Label>{sectionTitle}</Typography.Label>
            <ul>
              {rows.map((r) => (
                <li key={r}>{r}</li>
              ))}
            </ul>
          </section>
        );
      })}
      <p>Данные реестра на {formatDate(verdict.data_date)}</p>
      <div className="actions">
        {onWatch !== undefined && verdict.number !== null && (
          <Button onClick={onWatch} loading={watching} disabled={watching}>
            На контроль
          </Button>
        )}
        {card !== null && (
          <a
            href={card.registry_url}
            target="_blank"
            rel="noreferrer"
            onClick={(e) => {
              if (openExternal(card.registry_url)) e.preventDefault();
            }}
          >
            Открыть в реестре
          </a>
        )}
      </div>
    </article>
  );
}
