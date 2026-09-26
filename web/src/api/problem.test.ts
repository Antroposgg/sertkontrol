import { describe, expect, it } from 'vitest';

import { ApiError, problemMessage, toProblem } from './problem';

describe('toProblem', () => {
  it('разбирает RFC 9457', () => {
    const p = toProblem(
      { type: 'https://x/errors/expired', title: 'Expired', status: 401, detail: 'старая initData', code: 'init_data_expired' },
      401,
      'Unauthorized',
    );
    expect(p).toEqual({
      type: 'https://x/errors/expired',
      title: 'Expired',
      status: 401,
      detail: 'старая initData',
      code: 'init_data_expired',
    });
  });

  it('дополняет не-JSON тело из статуса', () => {
    expect(toProblem(undefined, 502, 'Bad Gateway')).toEqual({ type: 'about:blank', title: 'Bad Gateway', status: 502 });
    expect(toProblem('text', 500, '')).toEqual({ type: 'about:blank', title: 'HTTP 500', status: 500 });
  });

  it('игнорирует поля неверного типа', () => {
    expect(toProblem({ title: 42, detail: null }, 400, 'Bad Request')).toEqual({
      type: 'about:blank',
      title: 'Bad Request',
      status: 400,
    });
  });
});

describe('problemMessage', () => {
  it('просит переоткрыть приложение при устаревшей initData', () => {
    expect(problemMessage({ type: 'x', title: 't', status: 401, code: 'init_data_expired' })).toMatch(/снова откройте/);
    expect(problemMessage({ type: 'x', title: 't', status: 401, code: 'unauthorized' })).toMatch(/снова откройте/);
  });

  it('сеть и лимит', () => {
    expect(problemMessage({ type: 'x', title: 't', status: 0, code: 'network' })).toMatch(/Нет связи/);
    expect(problemMessage({ type: 'x', title: 't', status: 429, code: 'rate_limited' })).toMatch(/через минуту/);
  });

  it('иначе detail, затем title', () => {
    expect(problemMessage({ type: 'x', title: 'T', status: 400, detail: 'D' })).toBe('D');
    expect(problemMessage({ type: 'x', title: 'T', status: 400 })).toBe('T');
  });
});

describe('ApiError', () => {
  it('несёт problem и сообщение', () => {
    const e = new ApiError({ type: 'x', title: 'T', status: 404, detail: 'нет' });
    expect(e).toBeInstanceOf(Error);
    expect(e.name).toBe('ApiError');
    expect(e.message).toBe('нет');
    expect(e.problem.status).toBe(404);
    expect(new ApiError({ type: 'x', title: 'T', status: 404 }).message).toBe('T');
  });
});
