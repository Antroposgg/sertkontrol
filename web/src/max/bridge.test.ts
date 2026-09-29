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
