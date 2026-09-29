/**
 * Доступ к MAX Bridge (`window.WebApp`).
 *
 * Скрипт MAX Bridge подключается на этапе 1 после сверки с актуальной документацией MAX.
 * До этого (и вне клиента MAX, например в браузере разработчика) объекта нет,
 * и функции возвращают `undefined` вместо исключения.
 */

/** Подмножество MAX Bridge, которое использует мини-приложение. */
export interface MaxWebApp {
  /** Сырая строка initData: передаётся на сервер как есть в `X-Max-Init-Data` (АРХ §8). */
  initData?: string;
  /** Платформа запуска: `ios`, `android`, `desktop`, `web`. */
  platform?: string;
  /** Разобранная initData; `start_param` — параметр запуска из кнопки `open_app` бота. */
  initDataUnsafe?: { start_param?: string };
  /** Открыть внешнюю ссылку средствами MAX (dev.max.ru/docs/webapps/bridge). */
  openLink?: (url: string) => void;
}

declare global {
  interface Window {
    WebApp?: MaxWebApp;
  }
}

/**
 * Возвращает объект MAX Bridge, если мини-приложение открыто внутри MAX.
 * @param win окно (инъекция для тестов).
 */
export function getWebApp(win: Window = window): MaxWebApp | undefined {
  return win.WebApp;
}

/**
 * Сырая строка initData или `undefined`, если приложение открыто вне MAX.
 * @param win окно (инъекция для тестов).
 */
export function getInitData(win: Window = window): string | undefined {
  const raw = getWebApp(win)?.initData;
  return raw === undefined || raw === '' ? undefined : raw;
}

/**
 * Открывает внешнюю ссылку: внутри MAX — через `WebApp.openLink`, вне MAX — возвращает `false`,
 * и ссылку открывает браузер обычным переходом.
 * @param win окно (инъекция для тестов).
 */
export function openExternal(url: string, win: Window = window): boolean {
  const open = getWebApp(win)?.openLink;
  if (open === undefined) {
    return false;
  }
  open(url);
  return true;
}

/** Куда открыть мини-приложение по параметру запуска. */
export type StartTarget = { screen: 'portfolio' } | { screen: 'document'; number: string };

/**
 * Разбирает `start_param` кнопок бота: `portfolio` → «Портфель»; `doc-<hex UTF-8 номера>` → «Документ»
 * (в `start_param` MAX допускает только `[\w-]`, поэтому номер закодирован, см. `bot::document_start_param`).
 * @returns `undefined`, если параметра нет или он не распознан.
 */
export function parseStartParam(param: string | undefined): StartTarget | undefined {
  if (param === 'portfolio') {
    return { screen: 'portfolio' };
  }
  const hex = param?.startsWith('doc-') === true ? param.slice(4) : undefined;
  if (hex === undefined || hex.length === 0 || hex.length % 2 !== 0 || !/^[0-9a-f]+$/.test(hex)) {
    return undefined;
  }
  const bytes = new Uint8Array(hex.length / 2);
  for (let i = 0; i < bytes.length; i += 1) {
    bytes[i] = Number.parseInt(hex.slice(i * 2, i * 2 + 2), 16);
  }
  try {
    return { screen: 'document', number: new TextDecoder('utf-8', { fatal: true }).decode(bytes) };
  } catch {
    return undefined;
  }
}

/**
 * Параметр запуска мини-приложения, если оно открыто кнопкой бота.
 * @param win окно (инъекция для тестов).
 */
export function getStartTarget(win: Window = window): StartTarget | undefined {
  return parseStartParam(getWebApp(win)?.initDataUnsafe?.start_param);
}
