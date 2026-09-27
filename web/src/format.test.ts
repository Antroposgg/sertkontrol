import { describe, expect, it } from 'vitest';

import type { DocState } from './api/types';
import { describeChange, formatDate, simulateResultText } from './format';

const active: DocState = { status: 'active', status_name: 'действует', expiry_date: '2031-02-09', status_date: '2026-02-10' };
const suspended: DocState = {
  status: 'suspended',
  status_name: 'приостановлен',
  expiry_date: '2031-02-09',
  status_date: '2026-09-26',
};

describe('format', () => {
  it('formatDate', () => {
    expect(formatDate('2026-09-26')).toBe('26.09.2026');
    expect(formatDate('мусор')).toBe('мусор');
  });

  it('describeChange: смена статуса, появление, исчезновение, сроки', () => {
    const entry = { version: 2, data_date: '2026-09-26' };
    expect(describeChange({ ...entry, before: active, after: suspended })).toBe('действует → приостановлен с 26.09.2026');
    expect(describeChange({ ...entry, before: null, after: active })).toBe('появился в данных: действует с 10.02.2026');
    expect(describeChange({ ...entry, before: active, after: null })).toBe('исчез из данных (был: действует)');
    expect(describeChange({ ...entry, before: null, after: null })).toBe('исчез из данных');
    expect(describeChange({ ...entry, before: active, after: { ...active, expiry_date: '2032-01-01', status_date: null } })).toBe(
      'действует, изменились сроки (срок действия до 01.01.2032)',
    );
    expect(describeChange({ ...entry, before: active, after: { ...active, expiry_date: null } })).toBe(
      'действует, изменились сроки',
    );
  });

  it('simulateResultText', () => {
    expect(simulateResultText(0)).toMatch(/не изменились/);
    expect(simulateResultText(2)).toMatch(/Изменилось документов в портфеле: 2/);
  });
});
