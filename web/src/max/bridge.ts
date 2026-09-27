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
