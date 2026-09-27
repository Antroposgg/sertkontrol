import { describe, expect, it, vi } from 'vitest';

import { createApiClient } from './client';
import { ApiError } from './problem';

function jsonResponse(body: unknown, status = 200, contentType = 'application/json'): Response {
  return new Response(JSON.stringify(body), { status, headers: { 'Content-Type': contentType } });
}

describe('createApiClient', () => {
  it('GET отправляет initData и разбирает JSON', async () => {
    const fetchImpl = vi.fn<typeof fetch>().mockResolvedValue(jsonResponse({ ok: 1 }));
    const api = createApiClient({ getInitData: () => 'init', fetchImpl });
    await expect(api.get<{ ok: number }>('/me')).resolves.toEqual({ ok: 1 });
    const [url, init] = fetchImpl.mock.calls[0] ?? [];
    expect(url).toBe('/api/v1/me');
    const headers = new Headers(init?.headers);
    expect(headers.get('X-Max-Init-Data')).toBe('init');
    expect(init?.method).toBe('GET');
  });

  it('без initData заголовок не ставится', async () => {
    const fetchImpl = vi.fn<typeof fetch>().mockResolvedValue(jsonResponse({}));
    const api = createApiClient({ baseUrl: '/x', getInitData: () => undefined, fetchImpl });
    await api.get('/y');
    const [url, init] = fetchImpl.mock.calls[0] ?? [];
    expect(url).toBe('/x/y');
    expect(new Headers(init?.headers).has('X-Max-Init-Data')).toBe(false);
  });

  it('POST сериализует тело', async () => {
    const fetchImpl = vi.fn<typeof fetch>().mockResolvedValue(jsonResponse({ id: 1 }, 201));
    const api = createApiClient({ getInitData: () => 'i', fetchImpl });
    await expect(api.post('/portfolio', { number: 'RU D-1' })).resolves.toEqual({ id: 1 });
    const [, init] = fetchImpl.mock.calls[0] ?? [];
    expect(init?.body).toBe('{"number":"RU D-1"}');
    expect(new Headers(init?.headers).get('Content-Type')).toBe('application/json');
  });

  it('POST multipart передаёт FormData без Content-Type', async () => {
    const fetchImpl = vi.fn<typeof fetch>().mockResolvedValue(jsonResponse([]));
    const api = createApiClient({ getInitData: () => 'i', fetchImpl });
    const form = new FormData();
    form.append('file', new File(['x'], 'a.pdf'));
    await expect(api.postForm('/check/file', form)).resolves.toEqual([]);
    const [, init] = fetchImpl.mock.calls[0] ?? [];
    expect(init?.body).toBe(form);
    expect(new Headers(init?.headers).has('Content-Type')).toBe(false);
  });

  it('DELETE без тела ответа', async () => {
    const fetchImpl = vi.fn<typeof fetch>().mockResolvedValue(new Response(null, { status: 204 }));
    const api = createApiClient({ getInitData: () => 'i', fetchImpl });
    await expect(api.del('/portfolio/1')).resolves.toBeUndefined();
    expect(fetchImpl.mock.calls[0]?.[1]?.method).toBe('DELETE');
  });

  it('POST без тела и ответа', async () => {
    const fetchImpl = vi.fn<typeof fetch>().mockResolvedValue(new Response(null, { status: 204 }));
    const api = createApiClient({ getInitData: () => 'i', fetchImpl });
    await expect(api.postEmpty('/demo/reset')).resolves.toBeUndefined();
    const [url, init] = fetchImpl.mock.calls[0] ?? [];
    expect(url).toBe('/api/v1/demo/reset');
    expect(init?.method).toBe('POST');
    expect(init?.body).toBeUndefined();
  });

  it('ошибка RFC 9457 → ApiError с кодом', async () => {
    const fetchImpl = vi
      .fn<typeof fetch>()
      .mockResolvedValue(jsonResponse({ title: 'Gone', status: 401, code: 'init_data_expired' }, 401, 'application/problem+json'));
    const api = createApiClient({ getInitData: () => 'i', fetchImpl });
    const error: unknown = await api.get('/me').catch((e: unknown) => e);
    expect(error).toBeInstanceOf(ApiError);
    expect((error as ApiError).problem.code).toBe('init_data_expired');
  });

  it('ошибка с не-JSON телом', async () => {
    const fetchImpl = vi.fn<typeof fetch>().mockResolvedValue(new Response('oops', { status: 502, statusText: 'Bad Gateway' }));
    const api = createApiClient({ getInitData: () => 'i', fetchImpl });
    const error: unknown = await api.get('/me').catch((e: unknown) => e);
    expect((error as ApiError).problem).toEqual({ type: 'about:blank', title: 'Bad Gateway', status: 502 });
  });

  it('сбой сети → code network', async () => {
    const api = createApiClient({ getInitData: () => 'i', fetchImpl: vi.fn<typeof fetch>().mockRejectedValue(new TypeError('offline')) });
    const error: unknown = await api.get('/me').catch((e: unknown) => e);
    expect((error as ApiError).problem.code).toBe('network');
    expect((error as ApiError).problem.detail).toBe('offline');

    const api2 = createApiClient({ getInitData: () => 'i', fetchImpl: vi.fn<typeof fetch>().mockRejectedValue('raw') });
    const error2: unknown = await api2.get('/me').catch((e: unknown) => e);
    expect((error2 as ApiError).problem.detail).toBe('raw');
  });

  it('по умолчанию использует глобальный fetch', async () => {
    const spy = vi.spyOn(globalThis, 'fetch').mockResolvedValue(jsonResponse({ v: 1 }));
    const api = createApiClient({ getInitData: () => undefined });
    await expect(api.get('/data-status')).resolves.toEqual({ v: 1 });
    expect(spy).toHaveBeenCalledOnce();
    spy.mockRestore();
  });
});
