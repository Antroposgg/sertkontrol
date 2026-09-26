import { describe, expect, it } from 'vitest';

import { getInitData, getWebApp } from './bridge';

function fakeWindow(webApp?: Window['WebApp']): Window {
  return { WebApp: webApp } as unknown as Window;
}

describe('MAX Bridge', () => {
  it('возвращает initData внутри MAX', () => {
    expect(getInitData(fakeWindow({ initData: 'query_id=1&hash=abc' }))).toBe('query_id=1&hash=abc');
  });

  it('вне MAX initData нет', () => {
    expect(getWebApp(fakeWindow())).toBeUndefined();
    expect(getInitData(fakeWindow())).toBeUndefined();
  });

  it('пустая строка считается отсутствием initData', () => {
    expect(getInitData(fakeWindow({ initData: '' }))).toBeUndefined();
  });

  it('по умолчанию читает глобальный window', () => {
    expect(getInitData()).toBeUndefined();
  });
});
