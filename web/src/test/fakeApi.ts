import { vi } from 'vitest';

import type { SertkontrolApi } from '../api/sertkontrol';
import type { CheckedVerdict, DataStatus, PortfolioPage } from '../api/types';

export const verdict: CheckedVerdict = {
  check_id: 1,
  query: 'RU Д-CR.PA08.B.89369/26',
  number: 'RUD-CR.PA08.B.89369/26',
  display_number: 'RU Д-CR.PA08.B.89369/26',
  level: 'ok',
  data_date: '2026-09-25',
  snapshot_version: 1,
  is_demo: true,
  distance: 0,
  card: {
    kind: 'declaration',
    status: 'active',
    status_name: 'действует',
    issue_date: '2026-02-10',
    expiry_date: '2031-02-09',
    status_date: '2026-02-10',
    applicant_name: 'ООО «ТЕСТ»',
    applicant_inn: '7700000016',
    manufacturer_name: 'ТЕСТ-ЗАВОД',
    product: 'Чайники',
    tnved: '8516107100',
    registry_url: 'https://pub.fsa.gov.ru/rds/declaration',
  },
  findings: [
    { basis: 'fact', rule: 'status.active', text: 'Статус в реестре: действует' },
    { basis: 'calculation', rule: 'term.remaining', text: 'До окончания срока действия 1598 дн.' },
    { basis: 'recommendation', rule: 'advice.watch', text: 'Поставьте документ на контроль' },
  ],
  suggestions: [],
};

export const page: PortfolioPage = {
  items: [
    {
      id: 7,
      doc_key: 'RUD-CR.PA08.B.89369/26',
      display_number: 'RU Д-CR.PA08.B.89369/26',
      doc_kind: 'declaration',
      sku: 'SKU-1',
      supplier_inn: '7700000016',
      last_status: 'active',
      last_version: 1,
    },
  ],
  next_cursor: null,
};

export const dataStatus: DataStatus = {
  version: 1,
  source: 'demo',
  source_date: '2026-09-25',
  record_count: 16,
  is_demo: true,
  next_update: null,
};

/** API с успешными ответами по умолчанию; отдельные методы переопределяются в тесте. */
export function fakeApi(overrides: Partial<SertkontrolApi> = {}): SertkontrolApi {
  return {
    me: vi.fn(() =>
      Promise.resolve({ max_user_id: 1, portfolio_count: 1, is_demo: true, demo_stage: 'base' as const, consented: true }),
    ),
    check: vi.fn(() => Promise.resolve(verdict)),
    checkFile: vi.fn(() => Promise.resolve([verdict])),
    listPortfolio: vi.fn(() => Promise.resolve(page)),
    addToPortfolio: vi.fn(() => Promise.resolve({ item: page.items[0] ?? ({} as never), verdict })),
    removeFromPortfolio: vi.fn(() => Promise.resolve()),
    dataStatus: vi.fn(() => Promise.resolve(dataStatus)),
    ...overrides,
  };
}
