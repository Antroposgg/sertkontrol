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

describe('openExternal', () => {
  it('внутри MAX открывает через WebApp.openLink', async () => {
    const { openExternal } = await import('./bridge');
    const opened: string[] = [];
    const win = { WebApp: { openLink: (u: string) => opened.push(u) } } as unknown as Window;
    expect(openExternal('https://x', win)).toBe(true);
    expect(opened).toEqual(['https://x']);
    expect(openExternal('https://x', {} as Window)).toBe(false);
  });
});

describe('start_param', () => {
  it('разбирает портфель и документ', async () => {
    const { parseStartParam, getStartTarget } = await import('./bridge');
    expect(parseStartParam('portfolio')).toEqual({ screen: 'portfolio' });
    // «RU/1» — то же кодирование, что у bot::document_start_param.
    expect(parseStartParam('doc-52552f31')).toEqual({ screen: 'document', number: 'RU/1' });
    const cyr = Array.from(new TextEncoder().encode('RUC-RU.AЯ46.B.10005/24'), (b) => b.toString(16).padStart(2, '0')).join('');
    expect(parseStartParam(`doc-${cyr}`)).toEqual({ screen: 'document', number: 'RUC-RU.AЯ46.B.10005/24' });
    for (const bad of [undefined, '', 'doc-', 'doc-abc', 'doc-zz', 'doc-ff', 'check-5']) {
      expect(parseStartParam(bad)).toBeUndefined();
    }
    const win = { WebApp: { initDataUnsafe: { start_param: 'portfolio' } } } as unknown as Window;
    expect(getStartTarget(win)).toEqual({ screen: 'portfolio' });
    expect(getStartTarget({} as Window)).toBeUndefined();
  });
});

describe('сканер QR (F10)', () => {
  const qr = 'https://pub.fsa.gov.ru/rds/declaration/view/21950326/common';

  it('доступен только в мобильных клиентах с openCodeReader', async () => {
    const { canScanQr } = await import('./bridge');
    const reader = () => Promise.resolve(qr);
    expect(canScanQr(fakeWindow({ platform: 'ios', openCodeReader: reader }))).toBe(true);
    expect(canScanQr(fakeWindow({ platform: 'android', openCodeReader: reader }))).toBe(true);
    expect(canScanQr(fakeWindow({ platform: 'web', openCodeReader: reader }))).toBe(false);
    expect(canScanQr(fakeWindow({ platform: 'desktop', openCodeReader: reader }))).toBe(false);
    expect(canScanQr(fakeWindow({ platform: 'ios' }))).toBe(false);
    expect(canScanQr(fakeWindow())).toBe(false);
  });

  it('возвращает текст кода; отмена и пустой код — undefined', async () => {
    const { scanQr } = await import('./bridge');
    const calls: (boolean | undefined)[] = [];
    const win = fakeWindow({
      platform: 'android',
      openCodeReader: (fileSelect) => {
        calls.push(fileSelect);
        return Promise.resolve(` ${qr} `);
      },
    });
    expect(await scanQr(win)).toBe(qr);
    expect(calls).toEqual([true]);
    expect(await scanQr(fakeWindow({ platform: 'ios', openCodeReader: () => Promise.reject(new Error('cancel')) }))).toBeUndefined();
    expect(await scanQr(fakeWindow({ platform: 'ios', openCodeReader: () => Promise.resolve('  ') }))).toBeUndefined();
    expect(await scanQr(fakeWindow({ platform: 'web', openCodeReader: () => Promise.resolve(qr) }))).toBeUndefined();
  });
});
