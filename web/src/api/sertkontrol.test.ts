import { describe, expect, it, vi } from 'vitest';

import type { ApiClient } from './client';
import { createSertkontrolApi } from './sertkontrol';

function fakeClient(): ApiClient & { calls: unknown[][] } {
  const calls: unknown[][] = [];
  const record = (...args: unknown[]) => {
    calls.push(args);
    return Promise.resolve({} as never);
  };
  return {
    calls,
    get: vi.fn(record),
    post: vi.fn(record),
    postForm: vi.fn(record),
    postText: vi.fn(record),
    postEmpty: vi.fn((...args: unknown[]) => {
      calls.push(args);
      return Promise.resolve();
    }),
    del: vi.fn((...args: unknown[]) => {
      calls.push(args);
      return Promise.resolve();
    }),
  };
}

describe('createSertkontrolApi', () => {
  it('строит пути по openapi.yaml', async () => {
    const c = fakeClient();
    const api = createSertkontrolApi(c);
    await api.me();
    await api.check('RU Д-1/26');
    await api.listPortfolio({});
    await api.listPortfolio({ status: 'active', supplierInn: '7700000016', cursor: '5' });
    await api.listPortfolio({ supplierInn: '' });
    await api.addToPortfolio({ number: 'x' });
    await api.removeFromPortfolio(7);
    await api.dataStatus();
    await api.checkFile(new File(['%PDF'], 'a.pdf'));
    await api.history('ЕАЭС N RU Д-1/26');
    await api.simulateUpdate();
    await api.resetDemo();
    expect(c.calls.map((x) => x[0])).toEqual([
      '/me',
      '/check?number=RU+%D0%94-1%2F26',
      '/portfolio',
      '/portfolio?status=active&supplier_inn=7700000016&cursor=5',
      '/portfolio',
      '/portfolio',
      '/portfolio/7',
      '/data-status',
      '/check/file',
      '/history?number=%D0%95%D0%90%D0%AD%D0%A1+N+RU+%D0%94-1%2F26',
      '/demo/simulate-update',
      '/demo/reset',
    ]);
    const form = c.calls[8]?.[1];
    expect(form).toBeInstanceOf(FormData);
    expect((form as FormData).get('file')).toBeInstanceOf(File);
  });
});

describe('consent', () => {
  it('POST /me/consent без тела', async () => {
    const c = fakeClient();
    await createSertkontrolApi(c).consent();
    expect(c.calls).toEqual([['/me/consent']]);
  });
});

describe('importPortfolio', () => {
  it('POST /portfolio/import с text/csv', async () => {
    const c = fakeClient();
    await createSertkontrolApi(c).importPortfolio('a;b\n');
    expect(c.calls).toEqual([['/portfolio/import', 'a;b\n', 'text/csv; charset=utf-8']]);
  });
});

